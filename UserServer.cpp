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
const string update_status_op {"UpdateStatus"};
const string read_friend_list_op {"ReadFriendList"};

const string push_status_op {"PushStatus"};

const string prop_friends {"Friends"};
const string prop_status {"Status"};
const string prop_updates {"Updates"};

const string basic_url {"http://localhost:34568/"};
const string auth_url {"http://localhost:34570/"};
const string push_url {"http://localhost:34574/"};

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
  Function takes in a string (the user id)
  Returns a boolean: true if found, false if not found
*/
bool find_user(string user_id) {
  for (auto it = user_base.begin(); it != user_base.end(); it++) {
    if (it->first == user_id)
      return true;
  }
  return false;
}

/*
  Funtion returns the properties corresponding to the user in AuthTable
  Returns tuple containing token, partition, row

  Note: Only use this if find_user was already called and returned true
        This will return empty strings if the user is not found
*/
tuple<string,string,string> get_user_properties(string user_id) {
  for (auto it = user_base.begin(); it != user_base.end(); it++) {
    if (it->first == user_id) {
      return make_tuple(get<0>(it->second), get<1>(it->second), get<2>(it->second));
    }
  }
  return make_tuple("", "", "");
}

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

    //Store the information return from AuthServer for token, associated partition and associate row
    const string user_token {get_json_object_prop(auth_result.second, "token")};
    const string user_partition {get_json_object_prop(auth_result.second, "DataPartition")};
    const string user_row {get_json_object_prop(auth_result.second, "DataRow")};

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
    if (bool online {find_user(user_id)}) {
      cout << "User is already online" << endl;
      message.reply(status_codes::OK);
      return;
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
    
    /*
      Don't use the find_user function
      Modify the find_user function instead so it will remove the user and return
      If it does not return from that then the user is clearly not 'online'
    */

    // Find the user; if found remove from hashtable
    for (auto it = user_base.begin(); it != user_base.end(); it++) {
      if (it->first == user_id) {
        user_base.erase(it);
        cout << user_id << " is now offline" << endl;
        cout << "There are " << user_base.size() << " users still online" << endl;
        message.reply(status_codes::OK);
        return;
      }
    }

    // If did not return during the iteration then the user did not have an active session
    message.reply(status_codes::NotFound);
    return;
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

  const string user_id {paths[1]};

  // Cant do if (! bool online{find_user(user_id)}) so use this
  // Check if user has an active session
  bool online{find_user(user_id)};
  if (! online) {
    cout << user_id << " does not have an active session" << endl;
    message.reply(status_codes::Forbidden);
    return;    
  }

  if (paths[0] == read_friend_list_op) {

    tuple<string,string,string> user_creds {get_user_properties(user_id)};
    const string user_token {get<0>(user_creds)};
    const string user_partition {get<1>(user_creds)};
    const string user_row {get<2>(user_creds)};

    // Obtain the users properties through an authorized GET using BasicServer
    pair<status_code,value> user_prop {do_request (methods::GET,
                                                     basic_url +
                                                     read_entity_auth + "/" +
                                                     data_table_name + "/" +
                                                     user_token + "/" +
                                                     user_partition + "/" +
                                                     user_row)};
    assert(user_prop.first == status_codes::OK);

    string friend_list {get_json_object_prop(user_prop.second, prop_friends)};  

    cout << friend_list << endl;

    // Pair "Friends" with a string that contains the friends list then package it into a json value
    pair<string,string> new_friend_properties {make_pair (prop_friends, friend_list)};
    value json_friends {build_json_value(new_friend_properties)};

    message.reply(status_codes::OK,json_friends);
    return;
  }

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

  // All three operations have user_id as a parameter
  const string user_id {paths[1]};

  // Cant do if (! bool online{find_user(user_id)}) so use this
  // Check if user has an active session
  bool online{find_user(user_id)};
  if (! online) {
    cout << user_id << " does not have an active session" << endl;
    message.reply(status_codes::Forbidden);
    return;    
  }

  // Beyond this point it is assumed that the user has an active session (user is online)

  tuple<string,string,string> user_creds {get_user_properties(user_id)};
  const string user_token {get<0>(user_creds)};
  const string user_partition {get<1>(user_creds)};
  const string user_row {get<2>(user_creds)};

  // Obtain the users properties through an authorized GET using BasicServer
  pair<status_code,value> user_prop {do_request (methods::GET,
                                                   basic_url +
                                                   read_entity_auth + "/" +
                                                   data_table_name + "/" +
                                                   user_token + "/" +
                                                   user_partition + "/" +
                                                   user_row)};
  assert(user_prop.first == status_codes::OK);

  string friend_list {get_json_object_prop(user_prop.second, prop_friends)};

  if (paths[0] == add_friend_op) {

    const string friend_country {paths[2]};
    const string friend_name {paths[3]};

    // Check if the friend to add is already a friend
    friends_list_t user_friends {parse_friends_list(friend_list)};

    for (auto it = user_friends.begin(); it != user_friends.end(); it++) {
      if (it->first == friend_country && it->second == friend_name) {
        cout << friend_name << " from " << friend_country << " is already your friend" << endl;
        message.reply(status_codes::OK);
        return;
      }
      // Else it will iterate until the friend is found
      // If the friend is not found in the list then it will be added from from the code below
    }

    // If this far then friend was not found in the list, add friend
    user_friends.push_back(make_pair(friend_country,friend_name));

    // Update the string containing the friend list
    friend_list = friends_list_to_string(user_friends);

    // Build a new json value for the property "Friends" using the edited friend list
    pair<string,string> new_friend_properties {make_pair (prop_friends, friend_list)};
    value new_properties {build_json_value(new_friend_properties)};

    // Make a request to the BasicServer to update the property "Friends" for our user
    pair<status_code,value> update_properties {do_request (methods::PUT,
                                                   basic_url +
                                                   update_entity_auth + "/" +
                                                   data_table_name + "/" +
                                                   user_token + "/" +
                                                   user_partition + "/" +
                                                   user_row,
                                                   new_properties)};
    assert(update_properties.first == status_codes::OK);

    cout << "Added " << friend_name << " from " << friend_country << endl;
    cout << "New Friends Property: " << new_properties << endl;

    // Return what the PUT method gives; it should be OK and update the entity
    message.reply(update_properties.first);
    return;
  }

  if (paths[0] == un_friend_op) {

    const string friend_country {paths[2]};
    const string friend_name {paths[3]};

    // Check if the friend to be deleted is in the list
    friends_list_t user_friends {parse_friends_list(friend_list)};

    for (auto it = user_friends.begin(); it != user_friends.end(); it++) {
      // If found friend to remove
      if (it->first == friend_country && it->second == friend_name) {
    
        // Remove friend
        user_friends.erase(it);
    
        // Update the string containing the new list of friends after removing the specified friend
        friend_list = friends_list_to_string(user_friends);
    
        // Build a new json value for the property "Friends" using the edited friend list
        pair<string,string> new_friend_properties {make_pair (prop_friends, friend_list)};
        value new_properties {build_json_value(new_friend_properties)};

        // Make a request to the BasicServer to update the property "Friends" for our user
        pair<status_code,value> update_properties {do_request (methods::PUT,
                                                       basic_url +
                                                       update_entity_auth + "/" +
                                                       data_table_name + "/" +
                                                       user_token + "/" +
                                                       user_partition + "/" +
                                                       user_row,
                                                       new_properties)};
        assert(update_properties.first == status_codes::OK);

        cout << "Removed " << friend_name << " from " << friend_country << endl;
        cout << "New Friends Property: " << new_properties << endl;

        // Return what the PUT method gives; it should be OK and update the entity
        message.reply(update_properties.first);
        return;
      }
      // Else iterate until the friend is found or the list has been exhausted
    }

    // If the list has been exhausted then the friend is not in the list
    cout << friend_name << " from " << friend_country << " was not in your friends list" << endl;
    message.reply(status_codes::OK);
    return;
  }

  if (paths[0] == update_status_op) {

    const string user_new_status {paths[2]};

    // Build a new json value for the property "Status" using the edited status
    pair<string,string> new_status_properties {make_pair (prop_status, user_new_status)};
    value new_properties {build_json_value(new_status_properties)};
    cout << "New Status Property: " << new_properties << endl;

    // Make a request to the BasicServer to update the property "Status" for our user
    pair<status_code,value> update_status {do_request (methods::PUT,
                                                       basic_url +
                                                       update_entity_auth + "/" +
                                                       data_table_name + "/" +
                                                       user_token + "/" +
                                                       user_partition + "/" +
                                                       user_row,
                                                       new_properties)};
    assert(update_status.first == status_codes::OK);

    // Build a new json value for the property "Friends"
    pair<string,string> friend_properties {make_pair (prop_friends, friend_list)};
    value users_friends_to_update {build_json_value(friend_properties)};

    try {
      // Call PushServer to place the users new updated status into their friends "Updates" properties
      pair<status_code,value> push_status {do_request (methods::POST,
                                                       push_url +
                                                       push_status_op + "/" +
                                                       user_partition + "/" +
                                                       user_row + "/" +
                                                       user_new_status,
                                                       users_friends_to_update)};
      // Only return for push server is OK
      assert(update_status.first == status_codes::OK);
      message.reply(push_status.first);
      return;
    }

    catch (const web::uri_exception& e) {
      cout << "PushServer error: " << e.what() << endl;
      message.reply(status_codes::ServiceUnavailable);
      return;
    }
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

