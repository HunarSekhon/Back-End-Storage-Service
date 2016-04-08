/*
  Sample unit tests for BasicServer
 */

#include <algorithm>
#include <exception>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include <cpprest/http_client.h>
#include <cpprest/json.h>

#include <pplx/pplxtasks.h>

#include <UnitTest++/UnitTest++.h>

using std::cerr;
using std::cout;
using std::endl;
using std::make_pair;
using std::pair;
using std::string;
using std::vector;

using web::http::http_headers;
using web::http::http_request;
using web::http::http_response;
using web::http::method;
using web::http::methods;
using web::http::status_code;
using web::http::status_codes;
using web::http::uri_builder;

using web::http::client::http_client;

using web::json::object;
using web::json::value;

const string create_table_op {"CreateTableAdmin"};
const string delete_table_op {"DeleteTableAdmin"};

const string read_entity_admin {"ReadEntityAdmin"};
const string update_entity_admin {"UpdateEntityAdmin"};
const string delete_entity_admin {"DeleteEntityAdmin"};

const string read_entity_auth {"ReadEntityAuth"};
const string update_entity_auth {"UpdateEntityAuth"};

const string get_read_token_op  {"GetReadToken"};
const string get_update_token_op {"GetUpdateToken"};
const string get_update_data_op {"GetUpdateData"};

// The two optional operations from Assignment 1
const string add_property_admin {"AddPropertyAdmin"};
const string update_property_admin {"UpdatePropertyAdmin"};

const string sign_on_op {"SignOn"};
const string sign_off_op {"SignOff"};
const string add_friend_op {"AddFriend"};
const string un_friend_op {"UnFriend"};
const string update_status {"UpdateStatus"};
const string read_friend_list {"ReadFriendList"};

/*
  Make an HTTP request, returning the status code and any JSON value in the body

  method: member of web::http::methods
  uri_string: uri of the request
  req_body: [optional] a json::value to be passed as the message body

  If the response has a body with Content-Type: application/json,
  the second part of the result is the json::value of the body.
  If the response does not have that Content-Type, the second part
  of the result is simply json::value {}.

  You're welcome to read this code but bear in mind: It's the single
  trickiest part of the sample code. You can just call it without
  attending to its internals, if you prefer.
 */

// Version with explicit third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string, const value& req_body) {
  http_request request {http_method};
  if (req_body != value {}) {
    http_headers& headers (request.headers());
    headers.add("Content-Type", "application/json");
    request.set_body(req_body);
  }

  status_code code;
  value resp_body;
  http_client client {uri_string};
  client.request (request)
    .then([&code](http_response response)
    {
      code = response.status_code();
      const http_headers& headers {response.headers()};
      auto content_type (headers.find("Content-Type"));
      if (content_type == headers.end() ||
          content_type->second != "application/json")
        return pplx::task<value> ([] { return value {};});
      else
        return response.extract_json();
    })
.then([&resp_body](value v) -> void
    {
      resp_body = v;
      return;
    })
    .wait();
  return make_pair(code, resp_body);
}

// Version that defaults third argument
pair<status_code,value> do_request (const method& http_method, const string& uri_string) {
  return do_request (http_method, uri_string, value {});
}

/*
  Utility to create a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int create_table (const string& addr, const string& table) {
  pair<status_code,value> result {do_request (methods::POST, addr + create_table_op + "/" + table)};
  return result.first;
}

/*
  Utility to compare two JSON objects

  This is an internal routine---you probably want to call compare_json_values().
 */
bool compare_json_objects (const object& expected_o, const object& actual_o) {
  CHECK_EQUAL (expected_o.size (), actual_o.size());
  if (expected_o.size() != actual_o.size())
    return false;

  bool result {true};
  for (auto& exp_prop : expected_o) {
    object::const_iterator act_prop {actual_o.find (exp_prop.first)};
    CHECK (actual_o.end () != act_prop);
    if (actual_o.end () == act_prop)
      result = false;
    else {
      CHECK_EQUAL (exp_prop.second, act_prop->second);
      if (exp_prop.second != act_prop->second)
        result = false;
    }
  }
  return result;
}

/*
  Utility to compare two JSON objects represented as values

  expected: json::value that was expected---must be an object
  actual: json::value that was actually returned---must be an object
*/
bool compare_json_values (const value& expected, const value& actual) {
  assert (expected.is_object());
  assert (actual.is_object());

  object expected_o {expected.as_object()};
  object actual_o {actual.as_object()};
  return compare_json_objects (expected_o, actual_o);
}

/*
  Utility to compre expected JSON array with actual

  exp: vector of objects, sorted by Partition/Row property 
    The routine will throw if exp is not sorted.
  actual: JSON array value of JSON objects
    The routine will throw if actual is not an array or if
    one or more values is not an object.

  Note the deliberate asymmetry of the how the two arguments are handled:

  exp is set up by the test, so we *require* it to be of the correct
  type (vector<object>) and to be sorted and throw if it is not.

  actual is returned by the database and may not be an array, may not
  be values, and may not be sorted by partition/row, so we have
  to check whether it has those characteristics and convert it 
  to a type comparable to exp.
*/
bool compare_json_arrays(const vector<object>& exp, const value& actual) {
  /*
    Check that expected argument really is sorted and
    that every value has Partion and Row properties.
    This is a precondition of this routine, so we throw
    if it is not met.
  */
  auto comp = [] (const object& a, const object& b) -> bool {
  return a.at("Partition").as_string()  <  b.at("Partition").as_string()
         ||
         (a.at("Partition").as_string() == b.at("Partition").as_string() &&
          a.at("Row").as_string()       <  b.at("Row").as_string());  
  };
  if ( ! std::is_sorted(exp.begin(),
                         exp.end(),
                         comp))
    throw std::exception();

  // Check that actual is an array
  CHECK(actual.is_array());
  if ( ! actual.is_array())
    return false;
  web::json::array act_arr {actual.as_array()};

  // Check that the two arrays have same size
  CHECK_EQUAL(exp.size(), act_arr.size());
  if (exp.size() != act_arr.size())
    return false;

  // Check that all values in actual are objects
  bool all_objs {std::all_of(act_arr.begin(),
                             act_arr.end(),
                             [] (const value& v) { return v.is_object(); })};
  CHECK(all_objs);
  if ( ! all_objs)
    return false;

  // Convert all values in actual to objects
  vector<object> act_o {};
  auto make_object = [] (const value& v) -> object {
    return v.as_object();
  };
  std::transform (act_arr.begin(), act_arr.end(), std::back_inserter(act_o), make_object);

  /* 
     Ensure that the actual argument is sorted.
     Unlike exp, we cannot assume this argument is sorted,
     so we sort it.
   */
  std::sort(act_o.begin(), act_o.end(), comp);

  // Compare the sorted arrays
  bool eq {std::equal(exp.begin(), exp.end(), act_o.begin(), &compare_json_objects)};
  CHECK (eq);
  return eq;
}

/*
  Utility to create JSON object value from vector of properties
*/
value build_json_object (const vector<pair<string,string>>& properties) {
    value result {value::object ()};
    for (auto& prop : properties) {
      result[prop.first] = value::string(prop.second);
    }
    return result;
}

/*
  Utility to delete a table

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
 */
int delete_table (const string& addr, const string& table) {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
    addr + delete_table_op + "/" + table)};
  return result.first;
}

/*
  Utility to put an entity with a single property

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
  prop: Name of the property
  pstring: Value of the property, as a string
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row, const string& prop, const string& pstring) {
  pair<status_code,value> result {
    do_request (methods::PUT,
    addr + update_entity_admin + "/" + table + "/" + partition + "/" + row,
    value::object (vector<pair<string,value>>
                   {make_pair(prop, value::string(pstring))}))};
  return result.first;
}

/*
  Utility to put an entity with multiple properties

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
  props: vector of string/value pairs representing the properties

  Note: This was from Ted's repo but I changed UpdateEntity to UpdateEntityAdmin since it wants UpdateEntityAdmin in BasicServer.cpp
        I never tested this without changing it though, I just made the assumption that this change was needed
        -Trevor
 */
int put_entity(const string& addr, const string& table, const string& partition, const string& row,
              const vector<pair<string,value>>& props) {
  pair<status_code,value> result {
    do_request (methods::PUT,
               addr + "UpdateEntityAdmin/" + table + "/" + partition + "/" + row,
               value::object (props))};
  return result.first;
}

/*
  Utility to delete an entity

  addr: Prefix of the URI (protocol, address, and port)
  table: Table in which to insert the entity
  partition: Partition of the entity 
  row: Row of the entity
 */
int delete_entity (const string& addr, const string& table, const string& partition, const string& row)  {
  // SIGH--Note that REST SDK uses "methods::DEL", not "methods::DELETE"
  pair<status_code,value> result {
    do_request (methods::DEL,
    addr + delete_entity_admin + "/" + table + "/" + partition + "/" + row)};
  return result.first;
}

/*
  Utility to get a token good for updating a specific entry
  from a specific table for one day.
 */
pair<status_code,string> get_update_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_update_token_op + "/" +
                                              userid,
                                              pwd
                                              )};
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK)
    return make_pair (result.first, "");
  else {
    string token {result.second["token"].as_string()};
    return make_pair (result.first, token);
  }
}

/*
  Utility to get a token good for reading a specific entry
  from a specific table for one day.
 */
pair<status_code,string> get_read_token(const string& addr,  const string& userid, const string& password) {
  value pwd {build_json_object (vector<pair<string,string>> {make_pair("Password", password)})};
  pair<status_code,value> result {do_request (methods::GET,
                                              addr +
                                              get_read_token_op + "/" +
                                              userid,
                                              pwd
                                              )};
  cerr << "token " << result.second << endl;
  if (result.first != status_codes::OK)
    return make_pair (result.first, "");
  else {
    string token {result.second["token"].as_string()};
    return make_pair (result.first, token);
  }
}

/*
  A sample fixture that ensures TestTable exists, and
  at least has the entity Franklin,Aretha/USA
  with the property "Song": "RESPECT".

  The entity is deleted when the fixture shuts down
  but the table is left. See the comments in the code
  for the reason for this design.
 */
class GetFixture {
public:
  static constexpr const char* addr {"http://127.0.0.1:34568/"};
  static constexpr const char* table {"TestTable"};
  static constexpr const char* partition {"Franklin,Aretha"};
  static constexpr const char* row {"USA"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  GetFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
throw std::exception();
    }
  }
  ~GetFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
throw std::exception();
    }

    /*
In traditional unit testing, we might delete the table after every test.

However, in cloud NoSQL environments (Azure Tables, Amazon DynamoDB)
creating and deleting tables are rate-limited operations. So we
leave the table after each test but delete all its entities.
     */
    cout << "Skipping table delete" << endl;
    /*
    int del_result {delete_table(addr, table)};
    cerr << "delete result " << del_result << endl;
    if (del_result != status_codes::OK) {
throw std::exception();
    }
    */
  }
};


// Use for testing when you dont't want entities to exist within the table
class MyTest {
public:
  static constexpr const char* addr {"http://127.0.0.1:34568/"};
  static constexpr const char* table {"DontMakeThisTable"};
  static constexpr const char* partition {"Khaled,DJ"};
  static constexpr const char* row {"All_I_Do_Is_Win"};
  static constexpr const char* property {"Meme_Level"};
  static constexpr const char* prop_val {"Holy_Meme"};
};


class BasicFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* table {"TestTable"};
  static constexpr const char* partition {"USA"};
  static constexpr const char* row {"Franklin,Aretha"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  BasicFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }
  }

  ~BasicFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }

    /*
      In traditional unit testing, we might delete the table after every test.

      However, in cloud NoSQL environments (Azure Tables, Amazon DynamoDB)
      creating and deleting tables are rate-limited operations. So we
      leave the table after each test but delete all its entities.
    */
    cout << "Skipping table delete" << endl;
    /*
      int del_result {delete_table(addr, table)};
      cerr << "delete result " << del_result << endl;
      if (del_result != status_codes::OK) {
        throw std::exception();
      }
    */
  }
};


SUITE(GET) {
  /*
    A test of GET of a single entity
  */
  TEST_FIXTURE(GetFixture, GetSingle) {
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + GetFixture::table + "/"
      + GetFixture::partition + "/"
      + GetFixture::row)};
      
      CHECK_EQUAL(string("{\"")
      + GetFixture::property
      + "\":\""
      + GetFixture::prop_val
      + "\"}",
      result.second.serialize());
      CHECK_EQUAL(status_codes::OK, result.first);
    }

  /*
    A test of GET all table entries

    Demonstrates use of new compare_json_arrays() function.
   */
  TEST_FIXTURE(BasicFixture, GetAll) {
    string partition {"Canada"};
    string row {"Katherines,The"};
    string property {"Home"};
    string prop_val {"Vancouver"};
    int put_result {put_entity (BasicFixture::addr, BasicFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    assert (put_result == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(BasicFixture::addr)
      + read_entity_admin + "/"
      + string(BasicFixture::table))};
    CHECK_EQUAL(status_codes::OK, result.first);
    value obj1 {
      value::object(vector<pair<string,value>> {
          make_pair(string("Partition"), value::string(partition)),
          make_pair(string("Row"), value::string(row)),
          make_pair(property, value::string(prop_val))
      })
    };
    value obj2 {
      value::object(vector<pair<string,value>> {
          make_pair(string("Partition"), value::string(BasicFixture::partition)),
          make_pair(string("Row"), value::string(BasicFixture::row)),
          make_pair(string(BasicFixture::property), value::string(BasicFixture::prop_val))
      })
    };
    vector<object> exp {
      obj1.as_object(),
      obj2.as_object()
    };
    compare_json_arrays(exp, result.second);
    CHECK_EQUAL(status_codes::OK, delete_entity (BasicFixture::addr, BasicFixture::table, partition, row));
  }





  /*
    Test for assignment1 GET operation 1
    Begin Here

    Included are test for:  Missing table name
                            Missing partition name
                            Missing row name
                            Request line with a table that does not exist
                            Request with a specific property
  */


  // Test GET with a request that does not have a table name
  TEST_FIXTURE(MyTest, MissingTable) {
    cout << "\nTest for GET when the request is missing a table name" << endl;
    pair<status_code,value> result {
      do_request (methods::GET, 
      string(MyTest::addr)
      + read_entity_admin)};

    cout << "This was returned in result.first: " << result.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, result.first);
  }

  // Test GET with a request of that is missing a row name
  TEST_FIXTURE(GetFixture, MissingRow) {
    cout << "\nTest for GET when the request URI is missing a row name" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table) + "/"
      + string(GetFixture::partition))};

    cout << "Returned from the GET request" << endl;

    //cout << "This was returned in result.first: " << result.first << endl;
    //cout << "This was returned in result.second: " << result.second << endl;
    CHECK_EQUAL(status_codes::BadRequest, result.first);
  }


  // Test GET with a request of that is missing a partition name
  TEST_FIXTURE(GetFixture, MissingPartition) {
    cout << "\nTest for GET when the request URI is missing a partition name" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table) + "/"
      + string(GetFixture::row))};

    cout << "Returned from the GET request" << endl;

    //cout << "This was returned in result.first: " << result.first << endl;
    //cout << "This was returned in result.second: " << result.second << endl;
    CHECK_EQUAL(status_codes::BadRequest, result.first);
  }
  

  // Test get with a table name that doesn't exist in the table
  TEST_FIXTURE(MyTest, TableDoesNotExist){
    cout << "\nTest for GET when the request has a table name that does not exist" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
      string(MyTest::addr)
      + read_entity_admin + "/"
      + MyTest::table + "/"
      + MyTest::partition + "/"
      + MyTest::row)};

    cout << "This was returned in result.first: " << result.first << endl;
    cout << "This was returned in result.second: " << result.second << endl; 
    CHECK_EQUAL(status_codes::NotFound, result.first);
  }


  // Test get with a table name that closely matches an already existing table
  TEST_FIXTURE(MyTest, TableDoesNotExisterTwo){
    cout << "\nTest for GET when the request has a table name that does not exist" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
      string(MyTest::addr)
      + read_entity_admin + "/"
      + "TeStTaBlE" + "/"
      + MyTest::partition + "/"
      + MyTest::row)};

    cout << "This was returned in result.first: " << result.first << endl;
    cout << "This was returned in result.second: " << result.second << endl; 
    CHECK_EQUAL(status_codes::NotFound, result.first);
  }


  // Test request for GET to return a JSON body given a Partition name that does not exist within the table
  TEST_FIXTURE(MyTest, PartitionDoesNotExist) {
    cout << "\nTest for GET to return a JSON body given a specific Partition that does not exist in the table" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
        string(MyTest::addr)
        + read_entity_admin + "/"
        + "TestTable" + "/"
        + "fAkEpArTiOn" + "/"
        + "*")};

    CHECK_EQUAL(status_codes::NotFound, result.first);
    CHECK_EQUAL(0, result.second.as_array().size());
  }


  // Test request for GET to return a JSON body given a Partition name that does exist within the table
  TEST_FIXTURE(GetFixture, PartitionExists) {
    cout << "\nTest for GET to return a JSON body given a specific Partition that exists within the table" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
        string(GetFixture::addr)
        + read_entity_admin + "/"
        + string(GetFixture::table) + "/"
        + string(GetFixture::partition) + "/"
        + "*")};

    CHECK_EQUAL(status_codes::OK, result.first);
    CHECK_EQUAL(1, result.second.as_array().size());
  }


  // Test request for GET to return a JSON body given a specific Partition name that is associated with multiple rows (multiple properties / property values)
  TEST_FIXTURE(MyTest, PartitionExistsWithManyRows) {
    cout << "\nTest for GET to return a JSON body given a specific Partition that has multiple multiple rows (multiple property / property values" << endl;
    string partition {"Khaled,DJ"};
    string property {"Meme_Level"};

    string row1 {"All_I_Do_Is_Win"};
    string prop_val1 {"Holy_Meme"};

    string row2 {"Hold_You_Down"};
    string prop_val2 {"Dank_Meme"};

    string row3 {"How_Many_Times"};
    string prop_val3 {"Decent_Meme"};


    int putOne {put_entity (MyTest::addr, "TestTable", partition, row1, property, prop_val1)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    int putTwo {put_entity (MyTest::addr, "TestTable", partition, row2, property, prop_val2)};
    cerr << "put result " << putTwo << endl;
    assert (putTwo == status_codes::OK);

    int putThree {put_entity (MyTest::addr, "TestTable", partition, row3, property, prop_val3)};
    cerr << "put result " << putThree << endl;
    assert (putThree == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(MyTest::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table) + "/"
      + string(MyTest::partition) + "/"
      + "*")};

    CHECK_EQUAL(3,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row1));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row2));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row3));
  }


  // Test request for GET to return a JSON body given a Partition name in a table with more than one partition
  TEST_FIXTURE(GetFixture, MultiplePartitions) {

    // GetFixture has a constructor that gets called and places an entity into the table
    // The table placed into the table has a different partition than the one we are adding

    cout << "\nTest for GET to return a JSON body given a specific Partition in a table with multiple partitions" << endl;
    string partition {"Khaled,DJ"};
    string property {"Meme_Level"};

    string row1 {"All_I_Do_Is_Win"};
    string prop_val1 {"Holy_Meme"};

    string row2 {"Hold_You_Down"};
    string prop_val2 {"Dank_Meme"};

    string row3 {"How_Many_Times"};
    string prop_val3 {"Decent_Meme"};

    string partition4 {"Sabotage,Hippie"};
    string row4 {"Ridin_Solo"};
    string property4 {"Meme_Level"};
    string prop_val4 {"Not_Dank_Enough"};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row1, property, prop_val1)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    int putTwo {put_entity (MyTest::addr, "TestTable", partition, row2, property, prop_val2)};
    cerr << "put result " << putTwo << endl;
    assert (putTwo == status_codes::OK);

    int putThree {put_entity (MyTest::addr, "TestTable", partition, row3, property, prop_val3)};
    cerr << "put result " << putThree << endl;
    assert (putThree == status_codes::OK);

    int putFour {put_entity (MyTest::addr, "TestTable", partition4, row4, property4, prop_val4)};
    cerr << "put result " << putFour << endl;
    assert (putFour == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(MyTest::addr)
      + read_entity_admin + "/"
      + "TestTable" + "/"
      + partition4 + "/"
      + "*")};

    CHECK_EQUAL(1,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row1));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row2));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row3));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition4, row4));

    // GetFixture deletes the entity it adds so we do not have to delete it here
  }

  /*
  // Test request for GET to return a JSON body given a Partition that has empty strings for Property and Property Value
  TEST_FIXTURE(MyTest, PartitionWithEmptyStringPropertyAndValues) {
    cout << "\nTest for GET to return a JSON body given a Partition that has empty strings for Property and Property Value" << endl;
    string partition {"Khaled,DJ"};
    string row {"All_I_Do_Is_Win"};
    string property {""};
    string prop_val {""};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row, property, prop_val)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(MyTest::addr)
      + read_entity_admin + "/"
      + "TestTable" + "/"
      + partition + "/"
      + "*")};

    CHECK_EQUAL(1, result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
  }
  */

  /*
    Test for assignment1 GET operation 1
    End Here
  */





  /*
    Test for assignment1 GET operation 2
    Begin Here

    Included are test for:  Request line containing a specific property
                            Request line containing specific properties

    Note: Property value SHOULDNT have to match
          Only need to return entities with matching Properties 

          Ex: Suppose we have three entities
              1: Property = "Meme_Level" | Property Value = "*"
              2: Property = "Meme_Level" | Property Value = "*"
              3: Property = "Awards"     | Property Value = "*"
              If we send a request with Property = "Meme_Level" the returned JSON body should contain 1 and 2
              If we send a request with PRoperty = "Awards" the returned JSON body should only contain 3
  */
              
  // Test request for GET to return a JSON body given a one specific Property in a table that only has one entity
  TEST_FIXTURE(MyTest, OnePropertyOneEntity) {
    cout << "\nTest for GET to return a JSON body given a specific Property and Property Value when the table has one entity" << endl;
    string partition {"Khaled,DJ"};
    string row {"All_I_Do_Is_Win"};
    string property {"Meme_Level"};

    string prop_val_does_not_matter {"*"};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row, property, prop_val_does_not_matter)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table),value::object (vector<pair<string,value>>
             {make_pair(property, value::string(prop_val_does_not_matter))}))};;

    cout << "this was returned: " << result.second << endl;
    CHECK_EQUAL(1,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
  }


  // Test request for GET to return a JSON body given Property name in a table with two entities with the same property
  TEST_FIXTURE(MyTest, OnePropertyTwoEntities) {
    cout << "\nTest for GET to return a JSON body given a specific Property when the table has two entities with the same property" << endl;
    string partition {"Khaled,DJ"};
    string row {"All_I_Do_Is_Win"};
    string property {"Meme_Level"};

    string row2 {"Hold_You_Down"};

    string prop_val_does_not_matter {"*"};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row, property, prop_val_does_not_matter)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    int putTwo {put_entity (MyTest::addr, "TestTable", partition, row2, property, prop_val_does_not_matter)};
    cerr << "put result " << putTwo << endl;
    assert (putTwo == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table),value::object (vector<pair<string,value>>
             {make_pair(property, value::string(prop_val_does_not_matter))}))};;

    cout << "this was returned: " << result.second << endl;
    CHECK_EQUAL(2,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row2));
  }


  // Test request for GET to return a JSON body given Property name in a table with two entities with the same property
  TEST_FIXTURE(MyTest, TwoPropertiesOneEntityWithBoth) {
    cout << "\nTest for GET to return a JSON body given a specific Property when the table has two entities with the same property" << endl;
    string partition {"Khaled,DJ"};
    string row {"All_I_Do_Is_Win"};
    string property {"Meme_Level"};

    string partition2 {"Sabotage,Hippie"};
    string row2 {"Ridin_Solo"};
    string property2 {"Awards"};

    string prop_val3 {"All_The_Awards"};

    string prop_val_does_not_matter {"*"};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row, property, prop_val_does_not_matter)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    int putTwo {put_entity (MyTest::addr, "TestTable", partition2, row2, property2, prop_val_does_not_matter)};
    cerr << "put result " << putTwo << endl;
    assert (putTwo == status_codes::OK);

    int putThree {put_entity (MyTest::addr, "TestTable", partition, row, property2, prop_val_does_not_matter)};
    cerr << "put result " << putThree << endl;
    assert (putThree == status_codes::OK);

    vector<pair<string,value>> json_body;
    pair<string,value> pOne {make_pair(property, value::string(prop_val_does_not_matter))};
    pair<string,value> pTwo {make_pair(property2, value::string(prop_val_does_not_matter))};

    json_body.push_back(pOne);
    json_body.push_back(pTwo);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/" 
      + string(GetFixture::table),value::object (json_body))}; 

    cout << "this was returned: " << result.second << endl;
    CHECK_EQUAL(1,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition2, row2));
  }


  // Test request for GET to return a JSON body given Property name in a table with two entities with the same property
  TEST_FIXTURE(MyTest, TwoPropertiesTwoEntitiesWithBoth) {
    cout << "\nTest for GET to return a JSON body given a specific Property when the table has two entities with the same property" << endl;
    string partition {"Khaled,DJ"};
    string row {"All_I_Do_Is_Win"};
    string property {"Meme_Level"};

    string partition2 {"Sabotage,Hippie"};
    string row2 {"Ridin_Solo"};
    string property2 {"Awards"};

    string prop_val3 {"All_The_Awards"};

    string prop_val_does_not_matter {"*"};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row, property, prop_val_does_not_matter)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    int putTwo {put_entity (MyTest::addr, "TestTable", partition2, row2, property, prop_val_does_not_matter)};
    cerr << "put result " << putTwo << endl;
    assert (putTwo == status_codes::OK);

    int putThree {put_entity (MyTest::addr, "TestTable", partition2, row2, property2, prop_val_does_not_matter)};
    cerr << "put result " << putThree << endl;
    assert (putThree == status_codes::OK);

    int putFour {put_entity (MyTest::addr, "TestTable", partition, row, property2, prop_val_does_not_matter)};
    cerr << "put result " << putFour << endl;
    assert (putFour == status_codes::OK);

    vector<pair<string,value>> json_body;
    pair<string,value> pOne {make_pair(property, value::string(prop_val_does_not_matter))};
    pair<string,value> pTwo {make_pair(property2, value::string(prop_val_does_not_matter))};

    json_body.push_back(pOne);
    json_body.push_back(pTwo);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table),value::object (json_body))}; 

    cout << "this was returned: " << result.second << endl;
    CHECK_EQUAL(2,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition2, row2));
  }


  // Test request for GET to return a JSON body given two Properties in a table where no entities contain both properties
  TEST_FIXTURE(MyTest, TwoPropertiesZeroEntities) {
    cout << "\nTest for GET to return a JSON body given two Properties in a table where no entities contain both properties" << endl;
    string partition {"Khaled,DJ"};
    string row {"All_I_Do_Is_Win"};
    string property {"Meme_Level"};

    string partition2 {"Sabotage,Hippie"};
    string row2 {"Ridin_Solo"};
    string property2 {"Awards"};

    string prop_val_does_not_matter {"*"};

    int putOne {put_entity (MyTest::addr, "TestTable", partition, row, property, prop_val_does_not_matter)};
    cerr << "put result " << putOne << endl;
    assert (putOne == status_codes::OK);

    int putTwo {put_entity (MyTest::addr, "TestTable", partition2, row2, property2, prop_val_does_not_matter)};
    cerr << "put result " << putTwo << endl;
    assert (putTwo == status_codes::OK);

    vector<pair<string,value>> json_body;
    pair<string,value> pOne {make_pair(property, value::string(prop_val_does_not_matter))};
    pair<string,value> pTwo {make_pair(property2, value::string(prop_val_does_not_matter))};

    json_body.push_back(pOne);
    json_body.push_back(pTwo);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + read_entity_admin + "/"
      + string(GetFixture::table),value::object (json_body))}; 

    cout << "this was returned: " << result.second << endl;
    CHECK_EQUAL(status_codes::NotFound, result.first);
    CHECK_EQUAL(0,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition2, row2));
  }
  /*
    End of tests for GET
  */
}

class AuthFixture {
public:
  static constexpr const char* addr {"http://localhost:34568/"};
  static constexpr const char* auth_addr {"http://localhost:34570/"};
  static constexpr const char* userid {"user"};
  static constexpr const char* user_pwd {"user"};
  static constexpr const char* auth_table {"AuthTable"};
  static constexpr const char* auth_table_partition {"Userid"};
  static constexpr const char* auth_pwd_prop {"Password"};
  static constexpr const char* table {"DataTable"};
  static constexpr const char* partition {"USA"};
  static constexpr const char* row {"Franklin,Aretha"};
  static constexpr const char* property {"Song"};
  static constexpr const char* prop_val {"RESPECT"};

public:
  AuthFixture() {
    int make_result {create_table(addr, table)};
    cerr << "create result " << make_result << endl;
    if (make_result != status_codes::Created && make_result != status_codes::Accepted) {
      throw std::exception();
    }
    int put_result {put_entity (addr, table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    if (put_result != status_codes::OK) {
      throw std::exception();
    }
    // Ensure userid and password in system
    int user_result {put_entity (addr,
                                 auth_table,
                                 auth_table_partition,
                                 userid,
                                 auth_pwd_prop,
                                 user_pwd)};
    cerr << "user auth table insertion result " << user_result << endl;
    if (user_result != status_codes::OK)
      throw std::exception();
  }

  ~AuthFixture() {
    int del_ent_result {delete_entity (addr, table, partition, row)};
    if (del_ent_result != status_codes::OK) {
      throw std::exception();
    }

    int del_ent_result2 {delete_entity (addr, auth_table, auth_table_partition, userid)};
    if (del_ent_result2 != status_codes::OK) {
      throw std::exception();
    }
  }
};

SUITE(UPDATE_AUTH) {
  TEST_FIXTURE(AuthFixture,  PutAuth) {

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)};
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    CHECK_EQUAL(status_codes::OK, result.first);

    pair<status_code,value> ret_res {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_admin + "/"
                  + AuthFixture::table + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::OK, ret_res.first);
    value expect {
      build_json_object (
                         vector<pair<string,string>> {
                           added_prop,
                           make_pair(string(AuthFixture::property), 
                                     string(AuthFixture::prop_val))}
                         )};
                             
    cout << AuthFixture::property << endl;
    compare_json_values (expect, ret_res.second);
  }
}

SUITE(OBTAIN_TOKENS) {
  TEST_FIXTURE(AuthFixture, MissingUserId) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)};
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);
  
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       "",
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::BadRequest);
    // No need to delete the entity created as part of AuthFixtures routine as I added the delete for the entity inside the destructor
  }

  TEST_FIXTURE(AuthFixture, UserIdNotFound) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)};
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);
  
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       "FakeUserIdentifications",
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::NotFound);
    // No need to delete the entity created as part of AuthFixtures routine as I added the delete for the entity inside the destructor
  }

  TEST_FIXTURE(AuthFixture, IncorrectPassword) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)};
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);
  
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       "FakePassword")};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::NotFound);
    // No need to delete the entity created as part of AuthFixtures routine as I added the delete for the entity inside the destructor
  }

  TEST_FIXTURE(AuthFixture, MultiplePropertiesWithPassword) {
    // TODO
  }

  TEST_FIXTURE(AuthFixture, GoodTokenRequest) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    // Add a second user to AuthTable
    int addUser {put_entity (AuthFixture::addr,
                                 AuthFixture::auth_table,
                                 AuthFixture::auth_table_partition,
                                 "DJKhaled",
                                 AuthFixture::auth_pwd_prop,
                                 "PathWayToSuccess")};
    cerr << "user auth table insertion result " << addUser << endl;
    if (addUser != status_codes::OK)
      throw std::exception();

    // Add a third user to AuthTable
    int addUser2 {put_entity (AuthFixture::addr,
                                 AuthFixture::auth_table,
                                 AuthFixture::auth_table_partition,
                                 "Meme",
                                 AuthFixture::auth_pwd_prop,
                                 "Dank")};
    cerr << "user auth table insertion result " << addUser2 << endl;
    if (addUser2 != status_codes::OK)
      throw std::exception();  

    // Request a token for the original user
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {  
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Test the token we obtained
    pair<status_code,value> result1 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::OK, result1.first);

    pair<status_code,value> result2 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::OK, result2.first);

    CHECK_EQUAL(status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, "DJKhaled"));
    CHECK_EQUAL(status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, "Meme"));
    // No need to delete the entity created as part of AuthFixtures routine as I added the delete for the entity inside the destructor
  }
}

SUITE(GET_AUTH) {
  TEST_FIXTURE(AuthFixture, GetAuth) {

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)};
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> authResult {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::OK, authResult.first);

    pair<status_code,value> adminResult {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_admin + "/"
                  + AuthFixture::table + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::OK, adminResult.first);

    assert (compare_json_values(authResult.second, adminResult.second));
  }

  TEST_FIXTURE(AuthFixture, GetAuthBadRequest) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Missing token
    pair<status_code,value> result1 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::BadRequest, result1.first);

    // Missing row
    pair<status_code,value> result2 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition)};
    CHECK_EQUAL (status_codes::BadRequest, result2.first);

    // Missing partition
    pair<status_code,value> result3 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::BadRequest, result3.first);
  }

  TEST_FIXTURE(AuthFixture, GetAuthEntityNotFound) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Missing table
    pair<status_code,value> result1 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::NotFound, result1.first);

    // No entity with this partition and row name
    pair<status_code,value> result2 {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + "USA" + "/"
                  + "Khaled,DJ")};
    CHECK_EQUAL (status_codes::NotFound, result2.first);
  }

   TEST_FIXTURE(AuthFixture, GetAuthTokenNoAccess) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    // Add a second user to AuthTable
    int addUser {put_entity (AuthFixture::addr,
                                 AuthFixture::auth_table,
                                 AuthFixture::auth_table_partition,
                                 "DJKhaled",
                                 AuthFixture::auth_pwd_prop,
                                 "PathWayToSuccess")};
    cerr << "user auth table insertion result " << addUser << endl;
    if (addUser != status_codes::OK)
      throw std::exception();

    // Add DataPartition to the second user in AuthTable
    int putPartition2 {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            "DJKhaled",
                            "DataPartition",
                            "USA")}; 
    cerr << "put result " << putPartition2 << endl;
    assert (putPartition2 == status_codes::OK);

    // Add DataRow to the second user in AuthTable
    int putRow2 {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            "DJKhaled",
                            "DataRow",
                            "WeTheBest")}; 
    cerr << "put result " << putRow2 << endl;
    assert (putRow2 == status_codes::OK);
    
    // Obtain a token for the second user in AuthTable
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_read_token(AuthFixture::auth_addr,
                       "DJKhaled",
                       "PathWayToSuccess")};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Request GET for the properties of the first user with the token corresponding to the second user
    pair<status_code,value> getResult {
      do_request (methods::GET,
                  string(AuthFixture::addr)
                  + read_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::NotFound, getResult.first);
    CHECK_EQUAL (status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, "DJKhaled"));
    //CHECK_EQUAL (status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, "DJKhaled"));
    // Dont need to delete the first user in AuthTable as I altered Teds destructor for AuthFixture to delete the entity
  }
}


SUITE(PUT_AUTH) {
  TEST_FIXTURE(AuthFixture, PutAuthBadRequest) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Missing token
    pair<status_code,value> result1 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::BadRequest, result1.first);

    // Missing row
    pair<status_code,value> result2 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition)};
    CHECK_EQUAL (status_codes::BadRequest, result2.first);

    // Missing partition
    pair<status_code,value> result3 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::BadRequest, result3.first);
  }

  TEST_FIXTURE(AuthFixture, PutAuthEntityNotFound) {

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)};
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Missing table
    pair<status_code,value> result1 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL(status_codes::NotFound, result1.first);
  }

  TEST_FIXTURE(AuthFixture, PutAuthTokenNoAccess) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);

    // Add a second user to AuthTable
    int addUser {put_entity (AuthFixture::addr,
                                 AuthFixture::auth_table,
                                 AuthFixture::auth_table_partition,
                                 "DJKhaled",
                                 AuthFixture::auth_pwd_prop,
                                 "PathWayToSuccess")};
    cerr << "user auth table insertion result " << addUser << endl;
    if (addUser != status_codes::OK)
      throw std::exception();

    // Add DataPartition to the second user in AuthTable
    int putPartition2 {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            "DJKhaled",
                            "DataPartition",
                            "USA")}; 
    cerr << "put result " << putPartition2 << endl;
    assert (putPartition2 == status_codes::OK);

    // Add DataRow to the second user in AuthTable
    int putRow2 {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            "DJKhaled",
                            "DataRow",
                            "WeTheBest")}; 
    cerr << "put result " << putRow2 << endl;
    assert (putRow2 == status_codes::OK);
    
    // Obtain a token for the second user in AuthTable
    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       "DJKhaled",
                       "PathWayToSuccess")};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    // Request PUT for the properties of the first user with the token corresponding to the second user
    pair<status_code,value> ret_res {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_entity_auth + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row)};
    CHECK_EQUAL (status_codes::Forbidden, ret_res.first);
    CHECK_EQUAL(status_codes::OK, delete_entity (AuthFixture::addr, AuthFixture::auth_table, AuthFixture::auth_table_partition, "DJKhaled"));
    // Dont need to delete the first user in AuthTable as I altered Teds destructor for AuthFixture to delete the entity
  }
}

SUITE(NotImplemented) {
  TEST_FIXTURE(AuthFixture, ReturnNotImplemented) {

    /*
    Assume AuthTable already exists from curl since tables are rarely deleted
    AuthFixure makes sure that DataTable exists by trying to create it followed by placing an entity inside the table
    AuthFixture also adds an entity into AuthTable with the Partition "Userid" and the Row "user"
    */

    // Add DataPartition to the user in AuthTable
    int putPartition {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataPartition",
                            AuthFixture::partition)}; 
    cerr << "put result " << putPartition << endl;
    assert (putPartition == status_codes::OK);

    // Add DataRow to the user in AuthTable
    int putRow {put_entity (AuthFixture::addr,
                            AuthFixture::auth_table,
                            AuthFixture::auth_table_partition, 
                            AuthFixture::userid,
                            "DataRow",
                            AuthFixture::row)}; 
    cerr << "put result " << putRow << endl;
    assert (putRow == status_codes::OK);
    pair<string,string> added_prop {make_pair(string("born"),string("1942"))};

    cout << "Requesting token" << endl;
    pair<status_code,string> token_res {
      get_update_token(AuthFixture::auth_addr,
                       AuthFixture::userid,
                       AuthFixture::user_pwd)};
    cout << "Token response " << token_res.first << endl;
    CHECK_EQUAL (token_res.first, status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + update_property_admin + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    CHECK_EQUAL(status_codes::NotImplemented, result.first);
    
    pair<status_code,value> result2 {
      do_request (methods::PUT,
                  string(AuthFixture::addr)
                  + add_property_admin + "/"
                  + AuthFixture::table + "/"
                  + token_res.second + "/"
                  + AuthFixture::partition + "/"
                  + AuthFixture::row,
                  value::object (vector<pair<string,value>>
                                   {make_pair(added_prop.first,
                                              value::string(added_prop.second))})
                  )};
    CHECK_EQUAL(status_codes::NotImplemented, result2.first);
  }
}

/*
  This test class will insert three users (DJKhaled, Ted, Adebola) into both DataTable and AuthTable

  Associated with each user in the AuthTable are three properties: Password, DataPartition, DataRow
                                                                   Password is always "password" or standard_password
                                                                   DataPartition is always the users country (DJKhaled = USA, TED/Adebola = Canada)
                                                                   DataRow is always the users name (DJKhaled, Ted, Adebola)

  Associated with each user in the DataTAble are three proeprties: Friends, Status, Updates
                                                                   Each of the properties has an empty string as the property value to start with

  The destructor will remove all entities from both tables
  Each new TEST_FIXTURE will be starting from scratch

  You guys can change this if you want
*/

class SetUpFixture {
public:
  static constexpr const char* basic_url {"http://localhost:34568/"};
  static constexpr const char* auth_url {"http://localhost:34570/"};
  static constexpr const char* user_url {"http://localhost:34572/"};
  static constexpr const char* data_table_name {"DataTable"};
  static constexpr const char* auth_table_name {"AuthTable"};
  static constexpr const char* auth_table_partition {"Userid"};
  static constexpr const char* property_friends {"Friends"};
  static constexpr const char* property_status {"Status"};
  static constexpr const char* property_updates {"Updates"};
  static constexpr const char* property_partition {"DataPartition"};
  static constexpr const char* property_row {"DataRow"};
  static constexpr const char* property_password {"Password"};
  static constexpr const char* standard_password {"password"};
  static constexpr const char* empty_string {""};

public:
  SetUpFixture() {
    cout << "\n\nCreating DataTable" << endl;
    int create_data_table {create_table(basic_url, data_table_name)};
    cerr << "create result " << create_data_table << endl;
    if (create_data_table != status_codes::Created && create_data_table != status_codes::Accepted) {
      throw std::exception();
    }

    cout << "Creating AuthTable" << endl;
    int create_auth_table {create_table(auth_url, auth_table_name)};
    cerr << "create result " << create_auth_table << endl;
    if (create_auth_table != status_codes::Created && create_auth_table != status_codes::Accepted) {
      throw std::exception();
    }

    vector<pair<string,value>> data_properties;
    pair<string,value> friends_property {make_pair(property_friends,value::string(empty_string))};
    pair<string,value> status_property {make_pair(property_status,value::string(empty_string))};
    pair<string,value> updates_property {make_pair(property_updates,value::string(empty_string))};
    data_properties.push_back(updates_property);
    data_properties.push_back(status_property);
    data_properties.push_back(friends_property);

    cout << "Adding DJ Khaled into DataTable" << endl;
    int put_data_khaled {put_entity (basic_url, data_table_name, "USA", "DJKhaled", data_properties)};
    cerr << "put result " << put_data_khaled << endl;
    if (put_data_khaled != status_codes::OK) {
      throw std::exception();
    }

    cout << "Adding Ted into DataTable" << endl;
    int put_data_ted {put_entity (basic_url, data_table_name, "Canada", "Ted", data_properties)};
    cerr << "put result " << put_data_ted << endl;
    if (put_data_ted != status_codes::OK) {
      throw std::exception();
    }

    cout << "Adding Adebola into DataTable" << endl;
    int add_data_adebola {put_entity (basic_url, data_table_name, "Canada", "Adebola", data_properties)};
    cerr << "put result " << add_data_adebola << endl;
    if (add_data_adebola != status_codes::OK) {
      throw std::exception();
    }

    vector<pair<string,value>> auth_properties_khaled;
    pair<string,value> password_property_khaled {make_pair(property_password,value::string(standard_password))};
    pair<string,value> partition_property_khaled {make_pair(property_partition,value::string("USA"))};
    pair<string,value> row_property_khaled {make_pair(property_row,value::string("DJKhaled"))};
    auth_properties_khaled.push_back(password_property_khaled);
    auth_properties_khaled.push_back(partition_property_khaled); 
    auth_properties_khaled.push_back(row_property_khaled);
    
    vector<pair<string,value>> auth_properties_ted;
    pair<string,value> password_property_ted {make_pair(property_password,value::string(standard_password))};
    pair<string,value> partition_property_ted {make_pair(property_partition,value::string("Canada"))};
    pair<string,value> row_property_ted {make_pair(property_row,value::string("Ted"))};
    auth_properties_ted.push_back(password_property_ted);
    auth_properties_ted.push_back(partition_property_ted);
    auth_properties_ted.push_back(row_property_ted);
    
    vector<pair<string,value>> auth_properties_adebola;
    pair<string,value> password_property_adebola {make_pair(property_password,value::string(standard_password))};
    pair<string,value> partition_property_adebola {make_pair(property_partition,value::string("Canada"))};
    pair<string,value> row_property_adebola {make_pair(property_row,value::string("Ted"))};
    auth_properties_adebola.push_back(password_property_adebola);
    auth_properties_adebola.push_back(partition_property_adebola);
    auth_properties_adebola.push_back(row_property_adebola);

    cout << "Adding DJ Khaled into AuthTable" << endl;
    int put_auth_khaled {put_entity (basic_url, auth_table_name, auth_table_partition, "DJKhaled", auth_properties_khaled)};
    cerr << "put result " << put_auth_khaled << endl;
    if (put_auth_khaled != status_codes::OK) {
      throw std::exception();
    }

    cout << "Adding Ted into AuthTable" << endl;
    int put_auth_ted {put_entity (basic_url, auth_table_name, auth_table_partition, "Ted", auth_properties_ted)};
    cerr << "put result " << put_auth_ted << endl;
    if (put_auth_ted != status_codes::OK) {
      throw std::exception();
    }

    cout << "Adding Adebola into AuthTable" << endl;
    int add_auth_adebola {put_entity (basic_url, auth_table_name, auth_table_partition, "Adebola", auth_properties_adebola)};
    cerr << "put result " << add_auth_adebola << endl;
    if (add_auth_adebola != status_codes::OK) {
      throw std::exception();
    }
  }

  ~SetUpFixture() {
    cout << "Removing DJ Khaled from DataTable" << endl;
    int del_data_khaled {delete_entity (basic_url, data_table_name, "USA", "DJKhaled")};
    if (del_data_khaled != status_codes::OK) {
      throw std::exception();
    }

    cout << "Removing Ted from DataTable" << endl;
    int del_data_ted {delete_entity (basic_url, data_table_name, "Canada", "Ted")};
    if (del_data_ted != status_codes::OK) {
      throw std::exception();
    }

    cout << "Removing Adebola from DataTable" << endl;
    int del_data_adebola {delete_entity (basic_url, data_table_name, "Canada", "Adebola")};
    if (del_data_adebola != status_codes::OK) {
      throw std::exception();    
  }

    cout << "Removing DJ Khaled from AuthTable" << endl;
    int del_auth_khaled {delete_entity (basic_url, auth_table_name, auth_table_partition, "DJKhaled")};
    if (del_auth_khaled != status_codes::OK) {
      throw std::exception();
    }

    cout << "Removing Ted from AuthTable" << endl;
    int del_auth_ted {delete_entity (basic_url, auth_table_name, auth_table_partition, "Ted")};
    if (del_auth_ted != status_codes::OK) {
      throw std::exception();
    }

    cout << "Removing Adebola from AuthTable" << endl;
    int del_auth_adebola {delete_entity (basic_url, auth_table_name, auth_table_partition, "Adebola")};
    if (del_auth_adebola != status_codes::OK) {
      throw std::exception();    
    }
  }
};

/* Compile issues
SUITE(SignOn) {
  TEST_FIXTURE(SetUpFixture, BadSignOn) {
    // Construct the password for the one and only DJKhaled
    value wrong_password {build_json_object (vector<pair<string,string>> {make_pair(SetUpFixture::property_password,"ThisPasswordIsIncorrect")})};

    // Request to sign on as DJKhaled with the wrong password
    pair<status_code,value> password_res {
      do_request (methods::POST,
                  SetUpFixture::user_url +
                  sign_on_op + "/" +
                  "DJKhaled",
                  wrong_password)};

    CHECK_EQUAL(status_codes::NotFound,password_res.first);
  }
}
*/