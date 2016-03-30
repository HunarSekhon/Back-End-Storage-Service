  /*
 Basic Server code for CMPT 276, Spring 2016.
 */

#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <cpprest/base_uri.h>
#include <cpprest/http_listener.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <was/common.h>
#include <was/storage_account.h>
#include <was/table.h>

#include "TableCache.h"
#include "make_unique.h"

#include "azure_keys.h"

#include "ServerUtils.h"

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

constexpr const char* def_url = "http://localhost:34568";

const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};

const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string delete_entity_admin {"DeleteEntityAdmin"};

const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op  {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};


/*
  Cache of opened tables
 */
TableCache table_cache {};

/*
  Convert properties represented in Azure Storage type
  to prop_vals_t type.
 */
prop_vals_t get_properties (const table_entity::properties_type& properties, prop_vals_t values = prop_vals_t {}) {
  for (const auto v : properties) {
    if (v.second.property_type() == edm_type::string) {
      values.push_back(make_pair(v.first, value::string(v.second.string_value())));
    }
    else if (v.second.property_type() == edm_type::datetime) {
      values.push_back(make_pair(v.first, value::string(v.second.str())));
    }
    else if(v.second.property_type() == edm_type::int32) {
      values.push_back(make_pair(v.first, value::number(v.second.int32_value())));      
    }
    else if(v.second.property_type() == edm_type::int64) {
      values.push_back(make_pair(v.first, value::number(v.second.int64_value())));      
    }
    else if(v.second.property_type() == edm_type::double_floating_point) {
      values.push_back(make_pair(v.first, value::number(v.second.double_value())));      
    }
    else if(v.second.property_type() == edm_type::boolean) {
      values.push_back(make_pair(v.first, value::boolean(v.second.boolean_value())));      
    }
    else {
      values.push_back(make_pair(v.first, value::string(v.second.str())));
    }
  }
  return values;
}

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

/*
  Top-level routine for processing all HTTP GET requests.

  GET is the only request that has no command. All
  operands specify the value(s) to be retrieved.
 */
void handle_get(http_request message) { 
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** GET " << path << endl;
  auto paths = uri::split_path(path);

  // If command was ReadEntityAdmin
  if (paths[0] == read_entity_admin) {
    
    unordered_map<string,string> json_body {get_json_body (message)};

    // Need at least a table name
    if (paths.size() < 2) {
      cout << "Missing table name" << endl;
      message.reply(status_codes::BadRequest);
      return;
    }

    cloud_table table {table_cache.lookup_table(paths[1])};
    if ( ! table.exists()) {
      cout << "Table does not exist" << endl;
      message.reply(status_codes::NotFound);
      return;
    }

    /*
      Code for Operation 2

      Get entities containing properties specific to the ones in the body
      Size of paths is 2
    */
    if (paths.size() == 2 && json_body.size() > 0) {
      table_query query {};
      table_query_iterator end;
      table_query_iterator it = table.execute_query(query);
      vector<value> key_vec;
      vector<string> found_properties;

      // Push properties from json body of request into a vector called found_properties
      for(auto it_json = json_body.begin(); it_json != json_body.end(); ++it_json){
        found_properties.push_back(it_json->first);
      }

      //If flag = 0, properties does not match
      int flag = 0;
      while(it != end) {
        cout << "GET: " << it->partition_key() << " / " << it->row_key() << endl; 
        prop_vals_t keys { make_pair("Partition",value::string(it->partition_key())), make_pair("Row", value::string(it->row_key())) }; //saves partition
        keys = get_properties(it->properties(), keys);
        int i = 0;
        int j = 0;
        for(i = 0;i < found_properties.size();i++) {
          for(j = 0;j < keys.size();j++) {
            if(found_properties[i] == std::get<0>(keys[j])) {
              flag++;
              break;
            }
          }
        }

        //If correct number of properties found, save our partition into key_vec
        if(flag == found_properties.size()) {
          key_vec.push_back(value::object(keys)); 
        }
        
        //Reset flag for next partition
        flag = 0;
        ++it;
      }

      // If key_vec is empty then nothing was found; return NotFound and an empty body
      if (key_vec.size() == 0) {
        message.reply(status_codes::NotFound, value::array(key_vec));
        return;
      }

      // If key_vec is not empty then something was found; return OK with entities in a body
      message.reply(status_codes::OK, value::array(key_vec));
      return;
    }

    // GET all entries in table
    if (paths.size() == 2) {
      table_query query {};
      table_query_iterator end;
      table_query_iterator it = table.execute_query(query);
      vector<value> key_vec;
      while (it != end) {
        cout << "Key: " << it->partition_key() << " / " << it->row_key() << endl;
        prop_vals_t keys { make_pair("Partition",value::string(it->partition_key())), make_pair("Row", value::string(it->row_key())) };
        keys = get_properties(it->properties(), keys);
        key_vec.push_back(value::object(keys));
        ++it;
      }
      message.reply(status_codes::OK, value::array(key_vec));
      return;
    }

    // GET specific entry: Partition == paths[1], Row == paths[2]
    if (paths.size() != 4) {
      message.reply (status_codes::BadRequest);
      return;
    }

    /*
      Code for Operation 1

      GET all entities from a specific partition
      Size of paths is 4

      Note that Ted's code above this makes sure there are 4 parameters
      If the row is '*' it will get all entities within the specified partition
      If the row is not '*' it will skip this and go to Teds code where it will get all entities with the specified partition and row
    */
    if( paths[3] == "*" ) {
        table_query query {};
        table_query_iterator end;
        table_query_iterator it = table.execute_query(query);
        vector<value> key_vec;
        prop_vals_t keys;
        while(it != end) {
          if( paths[2] == it->partition_key() ) {
            cout << "GET: " << it->partition_key() << " / " << it->row_key() << endl; 
            keys = { make_pair("Partition",value::string(it->partition_key())), make_pair("Row",value::string(it->row_key())) };
            keys = get_properties(it->properties(), keys);
            key_vec.push_back(value::object(keys));
          }
          ++it;
        }

        // If key_vec is empty then nothing was found; return NotFound and an empty body
        if (key_vec.size() == 0) {
          message.reply(status_codes::NotFound, value::array(key_vec));
          return;
        }

        // If key_vec is not empty then something was found; return OK with entities in a body
        message.reply(status_codes::OK, value::array(key_vec));
        return;
    }

    table_operation retrieve_operation {table_operation::retrieve_entity(paths[2], paths[3])};
    table_result retrieve_result {table.execute(retrieve_operation)};
    cout << "HTTP code: " << retrieve_result.http_status_code() << endl;
    if (retrieve_result.http_status_code() == status_codes::NotFound) {
      message.reply(status_codes::NotFound);
      return;
    }

    table_entity entity {retrieve_result.entity()};
    table_entity::properties_type properties {entity.properties()};
    
    // If the entity has any properties, return them as JSON
    prop_vals_t values (get_properties(properties));
    if (values.size() > 0) {
      message.reply(status_codes::OK, value::object(values));
      return;
    }
    else {
      message.reply(status_codes::OK);
      return;
    }
  }
  

  /*
    Code for Assign2 Operation 1
  */
  // If command was ReadEntityAuth
  if (paths[0] == read_entity_auth) {

    // Parameter checking done by ServerUtils

    // Check if table exists
    cloud_table table {table_cache.lookup_table(paths[1])};
    if ( ! table.exists()) {
      cout << "Table does not exist" << endl;
      message.reply(status_codes::NotFound);
      return;
    }

    // Use function Ted made in ServerUtils.cpp
    pair<status_code,table_entity> result {read_with_token(message, tables_endpoint)};
    
    // read_with_token only returns OK as status_code if an entity was found with the given partition and row name
    if (result.first == status_codes::OK) {
      
      table_entity::properties_type properties {result.second.properties()};

      // If the entity has any properties, return them as JSON
      prop_vals_t values (get_properties(properties));
      if (values.size() > 0) {
        message.reply(result.first, value::object(values));
        return;
      }
      else {
        message.reply(result.first);
        return;
      }
    }

    else {
      message.reply(result.first);
      return;
    }
  }


  // Return BadRequest if GET operation does not match
  else { 
    message.reply(status_codes::BadRequest);
    return;
  }
}

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation and a table name
  if (paths.size() < 2) {
    message.reply(status_codes::BadRequest);
    return;
  }

  string table_name {paths[1]};
  cloud_table table {table_cache.lookup_table(table_name)};

  // Create table (idempotent if table exists)
  if (paths[0] == create_table) {
    cout << "Create " << table_name << endl;
    bool created {table.create_if_not_exists()};
    cout << "Administrative table URI " << table.uri().primary_uri().to_string() << endl;
    if (created)
      message.reply(status_codes::Created);
    else
      message.reply(status_codes::Accepted);
  }
  else {
    message.reply(status_codes::BadRequest);
  }
}

/*
  Top-level routine for processing all HTTP PUT requests.
 */
void handle_put(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** PUT " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation, table name, partition, and row
  if (paths.size() < 4) {
    message.reply(status_codes::BadRequest);
    return;
  }

  unordered_map<string,string> json_body {get_json_body (message)};  

  cloud_table table {table_cache.lookup_table(paths[1])};
  if ( ! table.exists()) {
    message.reply(status_codes::NotFound);
    return;
  }

  /*
    Coded for Assign2 Operation 2
    
    Check if command is for auth access
    Uses Teds code in ServerUtils to update
    Return
  */
  // If command was UpdateEntityAuth
  if (paths[0] == update_entity_auth) {
    message.reply(update_with_token(message, tables_endpoint, json_body));
    return;
  }  

  table_entity entity {paths[2], paths[3]};

  // Update entity
  try {
    if (paths[0] == update_entity_admin) {
      cout << "Update " << entity.partition_key() << " / " << entity.row_key() << endl;
      table_entity::properties_type& properties = entity.properties();
      for (const auto v : json_body) {
  properties[v.first] = entity_property {v.second};
      }

      table_operation operation {table_operation::insert_or_merge_entity(entity)};
      table_result op_result {table.execute(operation)};

      message.reply(status_codes::OK);
    }
    else {
      message.reply(status_codes::BadRequest);
    }
  }
  catch (const storage_exception& e)
  {
    cout << "Azure Table Storage error: " << e.what() << endl;
    message.reply(status_codes::InternalError);
  }
}

/*
  Top-level routine for processing all HTTP DELETE requests.
 */
void handle_delete(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** DELETE " << path << endl;
  auto paths = uri::split_path(path);
  // Need at least an operation and table name
  if (paths.size() < 2) {
  message.reply(status_codes::BadRequest);
  return;
  }

  string table_name {paths[1]};
  cloud_table table {table_cache.lookup_table(table_name)};

  // Delete table
  if (paths[0] == delete_table) {
    cout << "Delete " << table_name << endl;
    if ( ! table.exists()) {
      message.reply(status_codes::NotFound);
    }
    table.delete_table();
    table_cache.delete_entry(table_name);
    message.reply(status_codes::OK);
  }
  // Delete entity
  else if (paths[0] == delete_entity_admin) {
    // For delete entity, also need partition and row
    if (paths.size() < 4) {
  message.reply(status_codes::BadRequest);
  return;
    }
    table_entity entity {paths[2], paths[3]};
    cout << "Delete " << entity.partition_key() << " / " << entity.row_key()<< endl;

    table_operation operation {table_operation::delete_entity(entity)};
    table_result op_result {table.execute(operation)};

    int code {op_result.http_status_code()};
    if (code == status_codes::OK || 
  code == status_codes::NoContent)
      message.reply(status_codes::OK);
    else
      message.reply(code);
  }
  else {
    message.reply(status_codes::BadRequest);
  }
}

/*
  Main server routine

  Install handlers for the HTTP requests and open the listener,
  which processes each request asynchronously.
  
  Wait for a carriage return, then shut the server down.
 */
int main (int argc, char const * argv[]) {
  cout << "Parsing connection string" << endl;
  table_cache.init (storage_connection_string);

  cout << "Opening listener" << endl;
  http_listener listener {def_url};
  listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  listener.support(methods::PUT, &handle_put);
  listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop server." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "Closed" << endl;
}

