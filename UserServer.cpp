  /*
 User Server code for CMPT 276, Spring 2016.
 */

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <tuple>

#include <cpprest/base_uri.h>
#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <was/common.h>
#include <was/storage_account.h>
#include <was/table.h>

#include "TableCache.h"
#include "make_unique.h"

#include "ServerUtils.h"

#include "ClientUtils.h"

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

using pplx::extensibility::critical_section_t;
using pplx::extensibility::scoped_critical_section_t;

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

using prop_vals_t = vector<pair<string,value>>;

using std::get;
using std::make_tuple;
using std::tuple;

constexpr const char* def_url = "http://localhost:34572";

const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};

const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string delete_entity_admin {"DeleteEntityAdmin"};

const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op  {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data_op {"GetUpdateData"};

const string add_property_admin {"AddPropertyAdmin"};
const string update_property_admin {"UpdatePropertyAdmin"};

const string sign_on_op {"SignOn"};
const string sign_off_op {"SignOff"};
const string add_friend_op {"AddFriend"};
const string un_friend_op {"UnFriend"};
const string update_status {"UpdateStatus"};
const string read_friend_list {"ReadFriendList"};

const string basic_url {"http://localhost:34568/"};
const string auth_url {"http://localhost:34570/"};

const string auth_table_name {"AuthTable"};
const string data_table_name {"DataTable"};

/*
  Return true if an HTTP request has a JSON body

  This routine canbe called multiple times on the same message.
*/
/*bool has_json_body (http_request message) {
  return message.headers()["Content-type"] == "application/json";
}*/

/*
  Given an HTTP message with a JSON body, return the JSON
  body as an unordered map of strings to strings.

  If the message has no JSON body, return an empty map.

  THIS ROUTINE CAN ONLY BE CALLED ONCE FOR A GIVEN MESSAGE
  (see http://microsoft.github.io/cpprestsdk/classweb_1_1http_1_1http__request.html#ae6c3d7532fe943de75dcc0445456cbc7
  for source of this limit).

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

// Delcare the struture used to track whether or not a user is signed into the table
unordered_map<string,tuple<string,string,string>> user_base;

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);

  // SignOn and SignOff require exactly two parameters (command, userid); no more no less
  if (paths.size() != 2) {
    message.reply(status_codes::BadRequest);
    return;
  }

  // Both SignOn and SignOff have the user id as a parameter so set up a string and initialize it to the user id
  const string user_id {paths[1]};

  if (paths[0] == sign_on_op) {

    // Extract info from original message to obtain the password
    unordered_map<string,string> json_body {get_json_body (message)};
    string password {json_body["Password"]};

    // Check the AuthTable and obtain a token for the session if the user is found
    pair<status_code,value> auth_result {do_request (methods::GET,
                                                    auth_url +
                                                    get_update_data_op + "/" +
                                                    user_id,
                                                    value::object (vector<pair<string,value>>
                                                                    {make_pair("Password",
                                                                               value::string(password))})
                                                    )};

    // If AuthServer gives anything other than OK return NotFound; if OK then continue on
    if (auth_result.first != status_codes::OK) {
      message.reply(status_codes::NotFound);
      return;
    }

    // Store the information returned from AuthServer for token, associated partition and associated row
    string user_token {auth_result.second["token"].as_string()};
    string user_partition {auth_result.second["DataPartition"].as_string()};
    string user_row {auth_result.second["DataRow"].as_string()};

    // Check the DataTable if the entity corresponding to the partition and data obtained from AuthServer exists
    pair<status_code,value> basic_result {do_request (methods::GET,
                                                     basic_url +
                                                     read_entity_admin+ "/" +
                                                     data_table_name + "/" +
                                                     user_partition + "/" +
                                                     user_row)};

    // If BasicServer does not return OK with the above request then the user is not in DataTable
    if (basic_result.first != status_codes::OK) {
      message.reply(status_codes::NotFound);
      return;
    }

    // At this point, the user has been authenticated (has correct password) and is found in both Auth and Data tables
    // Therefore we can sign the user in (add the user to the hashtable)

    // Check if user is already online
    for (auto it = user_base.begin(); it != user_base.end(); it++) {
      if (it->first == user_id) {
        cout << "User is already online" << endl;
        message.reply(status_codes::OK);
        return;
      }
    }

    // If the user attempts to log in with the wrong credentials then it will return NotFound prior to this

    // Add user to hashtable (Now Online)
    tuple<string,string,string> properties {make_tuple(user_token, user_partition, user_row)};
    user_base.insert(make_pair(user_id, properties));

    cout << user_id << " is now online" << endl;
    cout << "There are currently " << user_base.size() << " users online" << endl;

    message.reply(status_codes::OK);
    return;
  }

  if (paths[0] == sign_off_op) {
    // TODO
  } 

  // No more accepted commands beyond this point
  // Return BadRequest due to possible malformed request
  message.reply(status_codes::BadRequest);
  return;
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_get(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** GET " << path << endl;
  auto paths = uri::split_path(path);

  // No more accepted commands beyond this point
  // Return BadRequest due to possible malformed request
  message.reply(status_codes::BadRequest);
  return;
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
  auto paths = uri::split_path(path);

  // Both operations AddFriend and UnFriend use the same parameters
  const string user_id {paths[1]};
  const string friend_country {paths[2]};
  const string friend_name {paths[3]};

  if (paths[0] == add_friend_op) {
    // TODO
  }

  if (paths[0] == un_friend_op) {
    // TODO
  }

  // No more accepted commands beyond this point
  // Return BadRequest due to possible malformed request
  message.reply(status_codes::BadRequest);
  return;
}

/*
  Main server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "UserServer Open" << endl;
  cout << "Parsing connection string" << endl;

  cout << "Opening listener" << endl;
  http_listener listener {def_url};
  listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop server." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "Closed" << endl;
}

