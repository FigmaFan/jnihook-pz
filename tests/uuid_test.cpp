#include "uuid.hpp"
#include <regex>
#include <unordered_set>
#include <cassert>
#include <iostream>

void TestUuidPattern()
{
        std::string uuid = GenerateUuid();
        std::regex pattern("^[0-9a-f]+_[0-9a-f]+_[0-9]+$");
        assert(std::regex_match(uuid, pattern));
}

void TestUuidCollisions()
{
        const int iterations = 10000;
        std::unordered_set<std::string> uuids;
        for (int i = 0; i < iterations; ++i) {
                auto uuid = GenerateUuid();
                bool inserted = uuids.insert(uuid).second;
                assert(inserted);
        }
}

int main()
{
        TestUuidPattern();
        TestUuidCollisions();
        std::cout << "UUID tests passed" << std::endl;
        return 0;
}
