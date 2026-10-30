#pragma once
#include <string>
#include <vector>
namespace catalog {
struct Episode { std::string title, path; };
struct Season { std::string name; std::vector<Episode> episodes; };
bool natural_less(const std::string &a,const std::string &b);
std::vector<Season> scan(const std::string &root);
}
