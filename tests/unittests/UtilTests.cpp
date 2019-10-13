#include "Test.h"

#include "slang/util/CommandLine.h"

TEST_CASE("Test CommandLine -- basic") {
    optional<bool> a, b, longFlag;
    optional<std::string> c;
    optional<int32_t> d;
    optional<uint64_t> ext;
    optional<double> ext2;
    optional<uint32_t> unused1;
    optional<int64_t> unused2;
    optional<uint32_t> used1;
    optional<int64_t> used2;
    std::vector<std::string> vals;

    CommandLine cmdLine;
    cmdLine.add("-a", a, "SDF");
    cmdLine.add("-b", b, "SDF");
    cmdLine.add("-z,-y,-x,--longFlag", longFlag, "SDF");
    cmdLine.add("-c", c, "SDF", "val");
    cmdLine.add("-d", d, "SDF", "val");
    cmdLine.add("-e,--ext", ext, "SDF", "val");
    cmdLine.add("-f,--ext2", ext2, "SDF", "val");
    cmdLine.add("--biz,--baz", unused1, "SDF", "val");
    cmdLine.add("--buz,--boz", unused2, "SDF", "val");
    cmdLine.add("--fiz,--faz", used1, "SDF", "val");
    cmdLine.add("--fuz,--foz", used2, "SDF", "val");
    cmdLine.setPositional(vals, "vals");

    CHECK(cmdLine.parse("prog -a -b --longFlag=False pos1 pos2 -c asdf -d -1234 --ext=9876 "
                        "--ext2 9999.1234e12 pos3 --fiz=4321 --foz=-4321    - pos5 "
                        "-- --buz --boz"sv));

    CHECK(cmdLine.getProgramName() == "prog");
    cmdLine.setProgramName("asdf");
    CHECK(cmdLine.getProgramName() == "asdf");

    CHECK(a);
    CHECK(b);
    CHECK(longFlag);
    CHECK(c);
    CHECK(d);
    CHECK(ext);
    CHECK(ext2);
    CHECK(!unused1);
    CHECK(!unused2);
    CHECK(used1);
    CHECK(used2);

    CHECK(*a == true);
    CHECK(*b == true);
    CHECK(*longFlag == false);
    CHECK(*c == "asdf");
    CHECK(*d == -1234);
    CHECK(*ext == 9876);
    CHECK(*ext2 == 9999.1234e12);
    CHECK(used1 == 4321);
    CHECK(used2 == -4321);

    REQUIRE(vals.size() == 7);
    CHECK(vals[0] == "pos1");
    CHECK(vals[1] == "pos2");
    CHECK(vals[2] == "pos3");
    CHECK(vals[3] == "-");
    CHECK(vals[4] == "pos5");
    CHECK(vals[5] == "--buz");
    CHECK(vals[6] == "--boz");
}

TEST_CASE("Test CommandLine -- vectors") {
    std::vector<int32_t> groupa;
    std::vector<int64_t> groupb;
    std::vector<uint32_t> groupc;
    std::vector<uint64_t> groupd;
    std::vector<double> groupe;
    std::vector<std::string> groupf;

    CommandLine cmdLine;
    cmdLine.add("-a,--longa", groupa, "SDF", "val");
    cmdLine.add("-b,--longb", groupb, "SDF", "val");
    cmdLine.add("-c,--longc", groupc, "SDF", "val");
    cmdLine.add("-d,--longd", groupd, "SDF", "val");
    cmdLine.add("-e,--longe", groupe, "SDF", "val");
    cmdLine.add("-f,--longf", groupf, "SDF", "val");

    CHECK(cmdLine.parse("prog -a 1 --longa 99 -f fff --longf=ffff -e 4.1 "
                        "-d 5 -d 5 -d 5 --longc 8 -c 9 -b -42 -b -43"sv));

    CHECK(groupa == std::vector{ 1, 99 });
    CHECK(groupb == std::vector{ -42ll, -43ll });
    CHECK(groupc == std::vector{ 8u, 9u });
    CHECK(groupd == std::vector{ 5ull, 5ull, 5ull });
    CHECK(groupe == std::vector{ 4.1 });
    CHECK(groupf == std::vector{ "fff"s, "ffff"s });
}

TEST_CASE("Test CommandLine -- splitting") {
    std::vector<std::string> stuff;

    CommandLine cmdLine;
    cmdLine.add("-a,--longa", stuff, "SDF", "val");

    auto args = R"(prog -a \ -a \-a asdf '--longa=bar baz bif \' -a "f foo \" biz \\" -a 1)"sv;
    CHECK(cmdLine.parse(args));

    CHECK(stuff == std::vector{ " -a"s, "asdf"s, "bar baz bif \\"s, "f foo \" biz \\"s, "1"s });
}

TEST_CASE("Test CommandLine -- programmer errors") {
    optional<bool> foo;

    CommandLine cmdLine;

    CHECK_THROWS(cmdLine.add("", foo, "SDF"));
    CHECK_THROWS(cmdLine.add(",--asdf1", foo, "SDF"));
    CHECK_THROWS(cmdLine.add("--asdf2,--asdf3,", foo, "SDF"));
    CHECK_THROWS(cmdLine.add("--asdf6,--asdf6", foo, "SDF"));
    CHECK_THROWS(cmdLine.add("foo", foo, "SDF"));
    CHECK_THROWS(cmdLine.add("-", foo, "SDF"));
    CHECK_THROWS(cmdLine.add("--", foo, "SDF"));
    CHECK_THROWS(cmdLine.add("-foo", foo, "SDF"));

    std::vector<std::string> vals;
    cmdLine.setPositional(vals, "vals");
    CHECK_THROWS(cmdLine.setPositional(vals, "vals2"));

    CHECK_THROWS(cmdLine.parse(string_view()));
}