/*
 Authorization Server code for CMPT 276, Spring 2016.
 */

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <was/common.h>
#include <was/table.h>

#include "TableCache.h"
#include "make_unique.h"

#include "azure_keys.h"

using azure::storage::cloud_storage_account;
using azure::storage::storage_credentials;
using azure::storage::storage_exception;
using azure::storage::cloud_table;
using azure::storage::cloud_table_client;
using azure::storage::edm_type;
using azure::storage::entity_property;
using azure::storage::table_entity;
using azure::storage::table_operation;
using azure::storage::table_query;
using azure::storage::table_query_iterator;
using azure::storage::table_result;
using azure::storage::table_request_options;
using azure::storage::table_shared_access_policy;

using std::cin;
using std::cout;
using std::endl;
using std::getline;
using std::make_pair;
using std::pair;
using std::string;
using std::unordered_map;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri;

using web::json::value;

using web::http::experimental::listener::http_listener;

using prop_str_vals_t = vector<pair<string,string>>;
using prop_vals_t = vector<pair<string,value>>;

constexpr const char* def_url = "http://localhost:34570";

const string auth_table_name {"AuthTable"};
const string auth_table_userid_partition {"Userid"};
const string auth_table_password_prop {"Password"};
const string auth_table_partition_prop {"DataPartition"};
const string auth_table_row_prop {"DataRow"};
const string data_table_name {"DataTable"};

const string get_read_token_op {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};

/*
  Cache of opened tables
 */
TableCache table_cache {};

/*
 Return a JSON object value whose (0 or more) properties are specified as a 
 vector of <string,string> pairs
 */
value build_json_value (const vector<pair<string,string>>& props) {
  vector<pair<string,value>> vals {};
  std::transform (props.begin(),
                  props.end(),
                  std::back_inserter(vals),
                  [] (const pair<string,string>& p) {
                    return make_pair(p.first, value::string(p.second));
                  });
  return value::object(vals);
}

/*
  Convert properties represented in Azure Storage type
  to prop_str_vals_t type.
 */
prop_str_vals_t get_string_properties (const table_entity::properties_type& properties) {
  prop_str_vals_t values {};
  for (const auto v : properties) {
    if (v.second.property_type() == edm_type::string) {
      values.push_back(make_pair(v.first,v.second.string_value()));
    }
    else {
      // Force the value as string in any case
      values.push_back(make_pair(v.first, v.second.str()));
    }
  }
  return values;
}

/*
  Given an HTTP message with a JSON body, return the JSON
  body as an unordered map of strings to strings.

  Note that all types of JSON values are returned as strings.
  Use C++ conversion utilities to convert to numbers or dates
  as necessary.
 */
unordered_map<string,string> get_json_body(http_request message) {  
  unordered_map<string,string> results {};
  const http_headers& headers {message.headers()};
  auto content_type (headers.find("Content-Type"));
  if (content_type == headers.end() ||
      content_type->second != "application/json")
    return results;

  value json{};
  message.extract_json(true)
    .then([&json](value v) -> bool
          {
            json = v;
            return true;
          })
    .wait();

  if (json.is_object()) {
    for (const auto& v : json.as_object()) {
      if (v.second.is_string()) {
        results[v.first] = v.second.as_string();
      }
      else {
        results[v.first] = v.second.serialize();
      }
    }
  }
  return results;
}

/*
  Return a token for 24 hours of access to the specified table,
  for the single entity defind by the partition and row.

  permissions: A bitwise OR ('|')  of table_shared_access_poligy::permission
    constants.

    For read-only:
      table_shared_access_policy::permissions::read
    For read and update: 
      table_shared_access_policy::permissions::read |
      table_shared_access_policy::permissions::update
 */
pair<status_code,string> do_get_token (const cloud_table& data_table,
                   const string& partition,
                   const string& row,
                   uint8_t permissions) {

  utility::datetime exptime {utility::datetime::utc_now() + utility::datetime::from_days(1)};
  try {
    string limited_access_token {
      data_table.get_shared_access_signature(table_shared_access_policy {
                                               exptime,
                                               permissions},
                                             string(), // Unnamed policy
                                             // Start of range (inclusive)
                                             partition,
                                             row,
                                             // End of range (inclusive)
                                             partition,
                                             row)
        // Following token allows read access to entire table
        //table.get_shared_access_signature(table_shared_access_policy {exptime, permissions})
      };
    cout << "Token " << limited_access_token << endl;
    return make_pair(status_codes::OK, limited_access_token);
  }
  catch (const storage_exception& e) {
    cout << "ERROR FROM do_get_token" << endl;
    cout << "Azure Table Storage error: " << e.what() << endl;
    cout << e.result().extended_error().message() << endl;
    return make_pair(status_codes::InternalError, string{});
  }
}

/*
  Return a JSON object value with a single property, specified
  as a pair of strings
 */
value build_json_value (const pair<string,string>& prop) {
  return value::object(vector<pair<string,value>> {
      make_pair (prop.first, value::string (prop.second))
    });
}

/*
  Top-level routine for processing all HTTP GET requests.
 */
void handle_get(http_request message) { 
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** AuthServer GET " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation and userid
  if (paths.size() < 2) {
    message.reply(status_codes::BadRequest);
    return;
  }

  // Check table
  cloud_table table {table_cache.lookup_table(auth_table_name)};
  if ( ! table.exists()) {
    cout << "Table does not exist" << endl;
    message.reply(status_codes::NotFound);
    return;
  }

  unordered_map<string,string> json_body {get_json_body(message)};

  // If command is GetReadToken
  if (paths[0] == get_read_token_op) {

    vector<string> prop, prop_val;

    // Store Password in prop_val
    for (auto it = json_body.begin(); it != json_body.end(); it++) {
      cout << "Property: " << it->first << ", PropertyValue: " << it->second << endl;
      prop.push_back(it->first);
      prop_val.push_back(it->second);
    }

    // Check to make sure Property passed into function was "Password"
    if (prop[0] != "Password") {
      message.reply(status_codes::BadRequest);
      return;
    }

    // Iterate AuthTable to find the matching user, check password, obtain partition/row, obtain key
    table_query query {};
    table_query_iterator end;
    table_query_iterator it_userid = table.execute_query(query);
    vector<value> key_vec;
    while (it_userid != end) {

      if (it_userid->row_key() == paths[1]) {

        // keys is returned as a pair | <i> if n == 0 then Property, if i == 1 then row | [i] == nth set of Property / Property Value
        prop_str_vals_t keys {get_string_properties(it_userid->properties())};

        // Go through the two properties (Password/Password or Partition/Row)
        for (int i = 0; i < keys.size(); i++) {

          // First find Property/PropertyValue that associates with the password
          if (std::get<0>(keys[i]) == "Password") {

            // Check if the password in the table matches the password in our message
            if (std::get<1>(keys[i]) == prop_val[0]) {
              cout << "Password provided was correct" << endl;
              
              // Go through the two proeprties (Password/Password or Partition/Row)
              for (int n = 0; n < keys.size(); n++) {

                // Find the Property/PropertyValue that associates with the Partition/Row we are trying to access in DataTable
                if (std::get<0>(keys[n]) != "Password") {

                  cout << "The partition: " << std::get<0>(keys[n]) << endl;
                  cout << "The row: " << std::get<1>(keys[n]) << endl;
                  // Once found, obtain the token
                  pair<status_code,string> result {do_get_token(table, std::get<0>(keys[n]), std::get<1>(keys[n]), 
                    table_shared_access_policy::permissions::read)};

                  pair<string,string> tokenPair {make_pair ("token", result.second)};
                  value token {build_json_value(tokenPair)};
                  message.reply(result.first, token);
                  return;
                }
                // There has to be a second field otherwise the insert was incorrect
              }
              // There has to be a second Property/PropertyValue to be found otherwise the table was missing something from the start
            }

            // If the password in the table does not match the password provided in the message
            else {
              cout << "Incorrect Password" << endl;
              message.reply(status_codes::NotFound);
              return;
            }

          }
          // There should be no else case, if the current Property/PropertyValue pair is not associated with the password then it just moves to the next pair
        }
        // If the user is found to be in the table
        // the only two returns should be from either an incorrect password or a successful request to obtain a token so nothing else is needed
      }

      // Go to next entity of AuthTable
      it_userid ++;
    }

    // If it leaves the while loop without then the user id was not found so we return the status code NotFound
    message.reply(status_codes::NotFound);
    return;
  }


  // If command is GetUpdateToken
  if (paths[0] == get_update_token_op) {

    vector<string> prop, prop_val;

    // Store Password in prop_val
    for (auto it = json_body.begin(); it != json_body.end(); it++) {
      cout << "Property: " << it->first << ", PropertyValue: " << it->second << endl;
      prop.push_back(it->first);
      prop_val.push_back(it->second);
    }

    // Check to make sure Property passed into function was "Password"
    if (prop[0] != "Password") {
      message.reply(status_codes::BadRequest);
      return;
    }

    // Iterate AuthTable to find the matching user, check password, obtain partition/row, obtain key
    table_query query {};
    table_query_iterator end;
    table_query_iterator it_userid = table.execute_query(query);
    vector<value> key_vec;
    while (it_userid != end) {

      if (it_userid->row_key() == paths[1]) {

        // keys is returned as a pair | <i> if n == 0 then Property, if i == 1 then row | [i] == nth set of Property / Property Value
        prop_str_vals_t keys {get_string_properties(it_userid->properties())};

        // Go through the two properties (Password/Password or Partition/Row)
        for (int i = 0; i < keys.size(); i++) {

          // First find Property/PropertyValue that associates with the password
          if (std::get<0>(keys[i]) == "Password") {

            // Check if the password in the table matches the password in our message
            if (std::get<1>(keys[i]) == prop_val[0]) {
              cout << "Password provided was correct" << endl;
              
              // Go through the two proeprties (Password/Password or Partition/Row)
              for (int n = 0; n < keys.size(); n++) {

                // Find the Property/PropertyValue that associates with the Partition/Row we are trying to access in DataTable
                if (std::get<0>(keys[n]) != "Password") {

                  cout << "The partition: " << std::get<0>(keys[n]) << endl;
                  cout << "The row: " << std::get<1>(keys[n]) << endl;
                  // Once found, obtain the token
                  pair<status_code,string> result {do_get_token(table, std::get<0>(keys[n]), std::get<1>(keys[n]), 
                    table_shared_access_policy::permissions::read | table_shared_access_policy::permissions::update)};

                  pair<string,string> tokenPair {make_pair ("token", result.second)};
                  value token {build_json_value(tokenPair)};
                  message.reply(result.first, token);
                  return;
                }
                // There has to be a second field otherwise the insert was incorrect
              }
              // There has to be a second Property/PropertyValue to be found otherwise the table was missing something from the start
            }

            // If the password in the table does not match the password provided in the message
            else {
              cout << "Incorrect Password" << endl;
              message.reply(status_codes::NotFound);
              return;
            }

          }
          // There should be no else case, if the current Property/PropertyValue pair is not associated with the password then it just moves to the next pair
        }
        // If the user is found to be in the table
        // the only two returns should be from either an incorrect password or a successful request to obtain a token so nothing else is needed
      }

      // Go to next entity of AuthTable
      it_userid ++;
    }

    // If it leaves the while loop without then the user id was not found so we return the status code NotFound
    message.reply(status_codes::NotFound);
    return;
  }

  message.reply(status_codes::NotImplemented);
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
}

/*
  Top-level routine for processing all HTTP DELETE requests.
 */
void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** DELETE " << path << endl;
}

/*
  Main authentication server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.

  Note that, unlike BasicServer, AuthServer only
  installs the listeners for GET. Any other HTTP
  method will produce a Method Not Allowed (405)
  response.

  If you want to support other methods, uncomment
  the call below that hooks in a the appropriate 
  listener.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "AuthServer: Parsing connection string" << endl;
  table_cache.init (storage_connection_string);

  cout << "AuthServer: Opening listener" << endl;
  http_listener listener {def_url};
  listener.support(methods::GET, &handle_get);
  //listener.support(methods::POST, &handle_post);
  //listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop AuthServer." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "AuthServer closed" << endl;
}
  