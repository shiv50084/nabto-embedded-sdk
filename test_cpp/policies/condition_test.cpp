#include <boost/test/unit_test.hpp>

#include <modules/policies/nm_condition.h>

#include <vector>
#include <utility>

BOOST_AUTO_TEST_SUITE(policies)

BOOST_AUTO_TEST_CASE(parse_bool)
{
    const char* f = "false";
    const char* t = "true";
    const char* invalid1 = "";
    const char* invalid2 = "foo";

    bool out;
    BOOST_TEST(nm_condition_parse_bool(f,&out));
    BOOST_TEST(out == false);

    BOOST_TEST(nm_condition_parse_bool(t,&out));
    BOOST_TEST(out == true);

    BOOST_TEST(!nm_condition_parse_bool(invalid1, &out));
    BOOST_TEST(!nm_condition_parse_bool(invalid2, &out));
}

BOOST_AUTO_TEST_CASE(parse_numeric)
{
    const char* n1 = "42";
    const char* n2 = "42.3";
    const char* invalid1 = "  ";
    const char* invalid2 = "foo";

    double out;
    BOOST_TEST(nm_condition_parse_numeric(n1,&out));
    BOOST_TEST(out == 42);

    BOOST_TEST(nm_condition_parse_numeric(n2,&out));
    BOOST_TEST(out == 42.3);

    BOOST_TEST(!nm_condition_parse_numeric(invalid1, &out));
    BOOST_TEST(!nm_condition_parse_numeric(invalid2, &out));
}

BOOST_AUTO_TEST_CASE(parse_condition_operator)
{
    std::vector<std::pair<std::string, enum nm_condition_operator> > cases;
    cases.push_back(std::make_pair("StringEquals", NM_CONDITION_OPERATOR_STRING_EQUALS));
    cases.push_back(std::make_pair("StringNotEquals", NM_CONDITION_OPERATOR_STRING_NOT_EQUALS));
    cases.push_back(std::make_pair("NumericEquals", NM_CONDITION_OPERATOR_NUMERIC_EQUALS));
    cases.push_back(std::make_pair("NumericNotEquals", NM_CONDITION_OPERATOR_NUMERIC_NOT_EQUALS));
    cases.push_back(std::make_pair("Bool", NM_CONDITION_OPERATOR_BOOL));

    for (auto c : cases) {
        enum nm_condition_operator op;
        BOOST_TEST(nm_condition_parse_operator(c.first.c_str(), &op) == true);
        BOOST_TEST(op == c.second);
    }

}

BOOST_AUTO_TEST_CASE(parse_condition_operator_fail)
{
    enum nm_condition_operator op;
    BOOST_TEST(nm_condition_parse_operator("foo", &op) == false);
}

BOOST_AUTO_TEST_CASE(condition_match)
{
    struct nm_condition c;
    nm_condition_init(&c);
    c.op = NM_CONDITION_OPERATOR_STRING_EQUALS;
    c.key = strdup("foo");
    np_string_set_add(&c.values, "bar");

    {
        struct np_string_map attributes;
        np_string_map_init(&attributes);

        BOOST_TEST(nm_condition_matches(&c, &attributes) == NM_CONDITION_RESULT_NO_MATCH);
        np_string_map_deinit(&attributes);
    }

    {
        struct np_string_map attributes;
        np_string_map_init(&attributes);
        np_string_map_insert(&attributes, "foo", "baz");
        BOOST_TEST(nm_condition_matches(&c, &attributes) == NM_CONDITION_RESULT_NO_MATCH);
        np_string_map_deinit(&attributes);
    }

    {
        struct np_string_map attributes;
        np_string_map_init(&attributes);

        np_string_map_insert(&attributes, "foo", "bar");
        BOOST_TEST(nm_condition_matches(&c, &attributes) == NM_CONDITION_RESULT_MATCH);
        np_string_map_deinit(&attributes);
    }
}

// BOOST_AUTO_TEST_CASE(condition_variable)
// {
//     Condition c(Condition::Operator::StringEquals, "IAM:UserId", {"${Connection:UserId}"});
//     Attributes a;

//     BOOST_TEST(c.matches(a) == Condition::Result::NO_MATCH);
//     a.set("IAM:UserId", "someuser");
//     BOOST_TEST(c.matches(a) == Condition::Result::NO_MATCH);
//     a.set("Connection:UserId", "somebar");
//     BOOST_TEST(c.matches(a) == Condition::Result::NO_MATCH);
//     a.set("Connection:UserId", "someuser");
//     BOOST_TEST(c.matches(a) == Condition::Result::MATCH);
// }

BOOST_AUTO_TEST_SUITE_END()
