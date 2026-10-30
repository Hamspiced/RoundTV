#include "catalog.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>

namespace catalog {
bool natural_less(const std::string &a,const std::string &b) {
    size_t i=0,j=0;
    while(i<a.size() && j<b.size()) {
        if(std::isdigit((unsigned char)a[i]) && std::isdigit((unsigned char)b[j])) {
            size_t ia=i,jb=j;
            while(i<a.size() && std::isdigit((unsigned char)a[i])) ++i;
            while(j<b.size() && std::isdigit((unsigned char)b[j])) ++j;
            while(ia<i && a[ia]=='0') ++ia;
            while(jb<j && b[jb]=='0') ++jb;
            if(i-ia!=j-jb) return i-ia<j-jb;
            int cmp=a.compare(ia,i-ia,b,jb,j-jb);
            if(cmp) return cmp<0;
        } else {
            int ca=std::tolower((unsigned char)a[i++]),cb=std::tolower((unsigned char)b[j++]);
            if(ca!=cb) return ca<cb;
        }
    }
    if(i!=a.size() || j!=b.size()) return i==a.size();
    return a<b; // stable tie break, e.g. 01 and 1
}
static bool is_directory(const std::string &p) { struct stat s{}; return !stat(p.c_str(),&s) && S_ISDIR(s.st_mode); }
static bool is_file(const std::string &p) { struct stat s{}; return !stat(p.c_str(),&s) && S_ISREG(s.st_mode); }
static std::vector<std::string> directories(const std::string &root) {
    std::vector<std::string> names;
    DIR *d=opendir(root.c_str()); if(!d) return names;
    while(auto *e=readdir(d)) {
        std::string name=e->d_name;
        if(name.empty() || name[0]=='.' || name[0]=='_') continue;
        if(is_directory(root+"/"+name)) names.push_back(name);
        if(names.size()>=2048) break; // bounded enumeration on an MCU
    }
    closedir(d); std::sort(names.begin(),names.end(),natural_less); return names;
}
std::vector<Season> scan(const std::string &root) {
    std::vector<Season> seasons;
    size_t total=0;
    for(auto &name:directories(root)) {
        // Season directories must begin with a number: 01, 02, or 01 - Name.
        if(!std::isdigit((unsigned char)name[0])) continue;
        Season s{name,{}};
        for(auto &episode:directories(root+"/"+name)) {
            std::string path=root+"/"+name+"/"+episode+"/video.avi";
            if(path.size()<480 && is_file(path)) { s.episodes.push_back({episode,path}); ++total; }
            if(s.episodes.size()>=512 || total>=1024) break;
        }
        if(!s.episodes.empty()) seasons.push_back(std::move(s));
        if(seasons.size()>=100 || total>=1024) break;
    }
    return seasons;
}
}
