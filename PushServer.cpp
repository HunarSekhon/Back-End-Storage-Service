  /*
 Push Server code for CMPT 276, Spring 2016.
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

constexpr const char* def_url = "http://localhost:34574";

const string create_table {"CreateTableAdmin"};
const string delete_table {"DeleteTableAdmin"};

const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string delete_entity_admin {"DeleteEntityAdmin"};

const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op  {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};

const string add_property_admin {"AddPropertyAdmin"};
const string update_property_admin {"UpdatePropertyAdmin"};

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

/*
  Top-level routine for processing all HTTP POST requests.
 */
void handle_post(http_request message) {
  string path {uri::decode(message.relative_uri().path())};
  cout << endl << "**** POST " << path << endl;
  auto paths = uri::split_path(path);

  if (paths[0] == push_status_op) {

    const string user_status {paths[2]};

    // Extract info from original message to obtain the password
    unordered_map<string,string> json_body {get_json_body (message)};
    string friend_list {json_body["Friends"]};

    // Obtain a vector containing all the information about the users friends
    friends_list_t user_friends {parse_friends_list(friend_list)};

    string friend_country, friend_name;

    auto it = user_friends.begin();
    while (it != user_friends.end()) {

      // Get the partition and the row of the friend
      friend_country = it->first;
      friend_name = it->second;

      // Obtain the users properties through an admin GET using BasicServer
      pair<status_code,value> friend_prop {do_request (methods::GET,
                                                       basic_url +
                                                       read_entity_admin + "/" +
                                                       data_table_name + "/" +
                                                       friend_country + "/" +
                                                       friend_name)};
      // Dont assert because no guarantees the friends are in the table
      // Check if the friend was in DataTable; should return OK if was inside
      if (friend_prop.first == status_codes::OK) {

        // Obtain a string correspondin to the friends current value for the property "Updates"
        string friend_updates {get_json_object_prop(friend_prop.second, prop_updates)};

        // Given the string for the friends current "Updates" property, concatenate the new status and add "\n" to the end of it
        friend_updates = friend_updates+user_status+"\n";

        // Build a new json value for the property "Friends" using the edited friend list
        pair<string,string> new_updates_property {make_pair(prop_updates, friend_updates)};
        value new_properties {build_json_value(new_updates_property)};

        // Make a request to the BasicServer to update the property "Updates" for the friend
        pair<status_code,value> update_properties {do_request (methods::PUT,
                                                       basic_url +
                                                       update_entity_admin + "/" +
                                                       data_table_name + "/" +
                                                       friend_country + "/" +
                                                       friend_name,
                                                       new_properties)};
        assert(update_properties.first == status_codes::OK);
      }

      // Go to next friend
      it++;
    }

    // After the while loop has completed, PushServer has made an attempt to update the property "Updates" for all friends of the user
    message.reply(status_codes::OK);
    return;
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
  cout << "PushServer Open" << endl;
  cout << "Parsing connection string" << endl;

  cout << "Opening listener" << endl;
  http_listener listener {def_url};
  //listener.support(methods::GET, &handle_get);
  listener.support(methods::POST, &handle_post);
  //listener.support(methods::PUT, &handle_put);
  //listener.support(methods::DEL, &handle_delete);
  listener.open().wait(); // Wait for listener to complete starting

  cout << "Enter carriage return to stop server." << endl;
  string line;
  getline(std::cin, line);

  // Shut it down
  listener.close().wait();
  cout << "Closed" << endl;
}

