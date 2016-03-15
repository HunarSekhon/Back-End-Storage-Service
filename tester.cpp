/*
  Sample unit tests for BasicServer
 */

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

using web::json::value;

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
  pair<status_code,value> result {do_request (methods::POST, addr + "CreateTable/" + table)};
  return result.first;
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
    addr + "DeleteTable/" + table)};
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
    addr + "UpdateEntity/" + table + "/" + partition + "/" + row,
    value::object (vector<pair<string,value>>
             {make_pair(prop, value::string(pstring))}))};
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
    addr + "DeleteEntity/" + table + "/" + partition + "/" + row)};
  return result.first;
}

/*
  A sample fixture that ensures TestTable exists, and
  at least has the entity Franklin,Aretha/USA
  with the property "Song": "RESPECT".

  The entity is deleted when the fixture shuts down
  but the table is left. See the comments in the code
  for the reason for this design.
 */
SUITE(GET) {
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

  /*
    A test of GET of a single entity
   */
  TEST_FIXTURE(GetFixture, GetSingle) {
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
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
   */
  TEST_FIXTURE(GetFixture, GetAll) {
    string partition {"Katherines,The"};
    string row {"Canada"};
    string property {"Home"};
    string prop_val {"Vancouver"};
    int put_result {put_entity (GetFixture::addr, GetFixture::table, partition, row, property, prop_val)};
    cerr << "put result " << put_result << endl;
    assert (put_result == status_codes::OK);

    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
      + string(GetFixture::table))};
    CHECK(result.second.is_array());
    CHECK_EQUAL(2, result.second.as_array().size());
    /*
      Checking the body is not well-supported by UnitTest++, as we have to test
      independent of the order of returned values.
     */
    //CHECK_EQUAL(body.serialize(), string("{\"")+string(GetFixture::property)+ "\":\""+string(GetFixture::prop_val)+"\"}");
    CHECK_EQUAL(status_codes::OK, result.first);
    CHECK_EQUAL(status_codes::OK, delete_entity (GetFixture::addr, GetFixture::table, partition, row));
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
      string(MyTest::addr))};

    cout << "This was returned in result.first: " << result.first << endl;
    CHECK_EQUAL(status_codes::BadRequest, result.first);
  }

  // Test GET with a request of that is missing a row name
  TEST_FIXTURE(GetFixture, MissingRow) {
    cout << "\nTest for GET when the request URI is missing a row name" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
      string(GetFixture::addr)
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
        + "TestTable" + "/"
        + "fAkEpArTiOn" + "/"
        + "*")};

    CHECK_EQUAL(status_codes::OK, result.first);
    CHECK_EQUAL(0, result.second.as_array().size());
  }


  // Test request for GET to return a JSON body given a Partition name that does exist within the table
  TEST_FIXTURE(GetFixture, PartitionExists) {
    cout << "\nTest for GET to return a JSON body given a specific Partition that exists within the table" << endl;
    pair<status_code,value> result {
      do_request (methods::GET,
        string(GetFixture::addr)
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
      + string(GetFixture::table),value::object (json_body))}; 

    cout << "this was returned: " << result.second << endl;
    CHECK_EQUAL(0,result.second.size());
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition, row));
    CHECK_EQUAL(status_codes::OK, delete_entity (MyTest::addr, "TestTable", partition2, row2));
  }
  /*
    End of tests for GET
  */
}

/*
  Locate and run all tests
 */
int main(int argc, const char* argv[]) {
  return UnitTest::RunAllTests();
}
