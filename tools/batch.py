"""Folder planning and cancellable conversion; independent of the GUI."""
from dataclasses import dataclass
from pathlib import Path
from types import SimpleNamespace
import json
import os
import re
import shutil
import subprocess
import tempfile
import threading
from prepare_media import safe_title, video

EXTENSIONS={'.mp4','.m4v','.mkv','.avi','.mov','.webm','.mpg','.mpeg','.ts','.m2ts','.wmv','.flv'}
class Cancelled(Exception): pass

def natural(value):
    return tuple((1,int(p)) if p.isdigit() else (0,p.casefold()) for p in re.split(r'(\d+)',str(value)))

@dataclass
class Job:
    source: Path
    season: int
    episode: int
    title: str
    status: str='Ready'
    def relative(self):
        return Path('videos/seasons')/f'{self.season:02d}'/f'{self.episode:02d} - {safe_title(self.title)}'/'video.avi'

def scan(root, default_season=1, start=1, auto=True, cancel=None):
    root=Path(root).resolve(); files=[]
    for directory,dirs,names in os.walk(root,followlinks=False):
        if cancel and cancel.is_set(): raise Cancelled()
        dirs[:]=sorted([d for d in dirs if not d.startswith('.') and not (Path(directory)/d).is_symlink()],key=natural)
        for name in names:
            p=Path(directory)/name
            if not name.startswith('.') and not p.is_symlink() and p.suffix.lower() in EXTENSIONS: files.append(p)
    files.sort(key=lambda p:natural(p.relative_to(root)))
    planned=[];reserved={}
    for p in files:
        season=default_season; episode=None;title=p.stem
        if auto:
            match=re.search(r'(?i)(?<![A-Za-z0-9])S(\d{1,2})[ ._-]*E(\d{1,3})(?!\d)',title)
            if not match:match=re.search(r'(?i)(?<!\d)(\d{1,2})x(\d{1,3})(?!\d)',title)
            if match:
                season,episode=map(int,match.groups());title=title[:match.start()]+' '+title[match.end():]
            else:
                for parent in [p.parent,*p.parent.parents]:
                    if parent==root.parent:break
                    m=re.fullmatch(r'(?i)(?:season[ _-]*|s)?(\d{1,2})',parent.name)
                    if m:season=int(m[1]);break
                m=re.match(r'^(?:[Ee])?(\d{1,3})[ ._-]+',title)
                if m:episode=int(m[1]);title=title[m.end():]
        title=safe_title(re.sub(r'[._]+',' ',title).strip(' -') or p.stem)
        if episode is not None:reserved.setdefault(season,set()).add(episode)
        planned.append([p,season,episode,title])
    next_numbers={};jobs=[]
    for p,season,episode,title in planned:
        if episode is None:
            n=next_numbers.get(season,start)
            while n in reserved.get(season,set()):n+=1
            episode=n;next_numbers[season]=n+1
        jobs.append(Job(p,season,episode,title))
    return jobs

def check_plan(jobs):
    seen=set()
    for job in jobs:
        if not 1<=job.season<=99 or not 1<=job.episode<=999:
            raise ValueError(f'{job.source.name}: season must be 1–99 and episode 1–999. Edit this row.')
        key=(job.season,job.episode)
        if key in seen:raise ValueError(f'Duplicate season {job.season}, episode {job.episode}. Edit or remove a row.')
        seen.add(key)
    if len(jobs)>1024:raise ValueError('The firmware supports at most 1,024 episodes. Reduce this batch.')
    counts={}
    for j in jobs:counts[j.season]=counts.get(j.season,0)+1
    if any(v>512 for v in counts.values()):raise ValueError('The firmware supports at most 512 episodes per season.')

def find_tools():
    for d in ['/opt/homebrew/bin','/usr/local/bin']:
        if d not in os.environ.get('PATH','').split(os.pathsep):os.environ['PATH']=d+os.pathsep+os.environ.get('PATH','')
    missing=[n for n in ['ffmpeg','ffprobe'] if not shutil.which(n)]
    if missing:raise ValueError('FFmpeg is missing. On macOS, install it with: brew install ffmpeg')

class Engine:
    def __init__(self, progress=lambda value:None):
        self.cancelled=threading.Event();self.lock=threading.Lock();self.process=None
        self.progress=progress;self.duration=0
    def cancel(self):
        self.cancelled.set()
        with self.lock:
            if self.process and self.process.poll() is None:
                try:self.process.terminate()
                except OSError:pass
        # Escalate only the FFmpeg/ffprobe process launched by this instance.
        def kill_later():
            with self.lock:p=self.process
            if p and p.poll() is None:
                try:p.kill()
                except OSError:pass
        timer=threading.Timer(2,kill_later);timer.daemon=True;timer.start()
    def guard(self):
        if self.cancelled.is_set():raise Cancelled()
    def popen(self,args,**kwargs):
        with self.lock:
            self.guard()
            self.process=subprocess.Popen([str(a) for a in args],stdin=subprocess.DEVNULL,
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0),**kwargs)
            return self.process
    def finish(self,p):
        if p.poll() is None:
            p.terminate()
            try:p.wait(timeout=2)
            except subprocess.TimeoutExpired:p.kill();p.wait()
        with self.lock:
            if self.process is p:self.process=None
    def probe(self,path):
        p=self.popen(['ffprobe','-v','error','-show_streams','-show_format','-of','json',path],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            while True:
                try:out,err=p.communicate(timeout=.2);break
                except subprocess.TimeoutExpired:self.guard()
            self.guard()
            if p.returncode:raise ValueError(err.strip()[-2000:] or 'Cannot read this media file.')
            data=json.loads(out)
            durations=[s.get('duration') for s in data.get('streams',[]) if s.get('codec_type')=='video']+[data.get('format',{}).get('duration')]
            for duration in durations:
                try:self.duration=float(duration);break
                except (TypeError,ValueError):pass
            return data
        finally:self.finish(p)
    def run(self,args):
        self.guard()
        command=[args[0],'-nostdin','-nostats','-progress','pipe:1',*args[1:]]
        with tempfile.TemporaryFile(mode='w+b') as errors:
            p=self.popen(command,stdout=subprocess.PIPE,stderr=errors,text=True,bufsize=1)
            try:
                for line in p.stdout:
                    self.guard()
                    if line.startswith('out_time_us=') and self.duration>0:
                        try:self.progress(min(.99,float(line.split('=',1)[1])/1_000_000/self.duration))
                        except ValueError:pass
                p.wait();self.guard()
                if p.returncode:
                    errors.seek(0,2);end=errors.tell();errors.seek(max(0,end-6000))
                    raise ValueError(errors.read().decode(errors='replace').strip() or 'FFmpeg conversion failed.')
            finally:
                self.finish(p);p.stdout.close()
    def convert(self,job,destination,fps=24,quality=5,overwrite=False):
        self.guard();self.duration=0
        output=Path(destination)/job.relative()
        if output.exists() and not overwrite:return 'Skipped (exists)'
        # Reject a source file already in the destination layout, including aliases.
        if job.source.resolve()==output.resolve():raise ValueError('Source and output are the same file.')
        args=SimpleNamespace(input=job.source,sd=Path(destination),season=job.season,episode=job.episode,
                             title=job.title,fps=fps,quality=quality,overwrite=overwrite)
        video(args,runner=self.run,prober=self.probe)
        return 'Done'
