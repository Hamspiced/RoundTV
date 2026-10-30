"""Host regression test for actual startup/tap code and catalog ordering."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'src/main.cpp').read_text()
boot = s.split('    // Catalog order is natural numeric order across seasons and episodes.')[1].split('    uint64_t last_state=0;')[0]
tap = s.split('        if(want_controls) {')[1].split('        if(problem=="Refreshing..."')[0]
tap = 'if(want_controls) {' + tap
assert 'static bool startup_waiting=true;' in s
assert 'static bool waiting=false' in s
assert 'startup_waiting=false;' in s.split('static void begin_selected()')[1].split('static void intermission()')[0]
with tempfile.TemporaryDirectory() as tmp:
    t = Path(tmp)
    for season, episode in [('10','10 - Last'),('2','10 - Second'),('2','2 - First')]:
        d=t/'media'/season/episode; d.mkdir(parents=True); (d/'video.avi').touch()
    cpp = '''
#include "catalog.hpp"
#include <vector>
#include <cassert>
#include <cstdio>
struct Selection { size_t season,episode; };
std::vector<catalog::Season> seasons;
std::vector<Selection> playlist;
size_t playlist_index=0;
bool startup_waiting=true,want_controls=false,waiting=false;
int gif=1,notice=0,started=0,controls=0,browse=0;
bool sd_ok=true; std::string problem;
namespace board {void backlight(bool){}}
void lv_label_set_text(int,const char*) {}
void set_menu(bool) {}
void begin_selected(){++started;startup_waiting=false;waiting=false;}
void show_controls(){++controls;}
void show_seasons(){++browse;}
void boot(){
''' + boot + '\n}\nvoid touch(){\n' + tap + '''
}
int main(int argc,char** argv){
    assert(argc==2);
    seasons=catalog::scan(argv[1]);boot();
    assert(startup_waiting && !waiting && started==0 && controls==0);
    assert(playlist.size()==3);
    assert(seasons[playlist[0].season].name=="2");
    assert(seasons[playlist[0].season].episodes[playlist[0].episode].title=="2 - First");
    assert(seasons[playlist[1].season].episodes[playlist[1].episode].title=="10 - Second");
    assert(seasons[playlist[2].season].name=="10");
    for(int i=0;i<10000;++i) touch();
    assert(started==0 && controls==0 && !waiting);
    want_controls=true;touch();
    assert(started==1 && !startup_waiting && !want_controls && controls==0 && playlist_index==0);
    want_controls=true;touch();assert(started==1 && controls==1);
    seasons.clear();playlist.clear();startup_waiting=true;gif=0;boot();
    assert(startup_waiting && !waiting);
    want_controls=true;touch();assert(browse==1 && started==1 && !startup_waiting);
    puts("PASS: startup gate, ordered catalog/playlist, first tap, later tap, empty library/missing GIF");
}
'''
    (t/'test.cpp').write_text(cpp)
    subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(t/'test.cpp'),str(root/'src/catalog.cpp'),'-o',str(t/'test')],check=True)
    subprocess.run([str(t/'test'),str(t/'media')],check=True)
