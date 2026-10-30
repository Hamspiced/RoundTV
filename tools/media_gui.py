"""Native Tk desktop interface for the RoundTV media converter."""
import queue
import threading
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from batch import Engine, Cancelled, scan, check_plan, find_tools

class App:
    def __init__(self,root):
        self.root=root;root.title('RoundTV Box Media Prep');root.geometry('1080x730');root.minsize(830,620)
        self.events=queue.Queue();self.jobs=[];self.busy=False;self.engine=None;self.closing=False;self.scanned_source=None
        self.source=tk.StringVar();self.dest=tk.StringVar();self.season=tk.IntVar(value=1);self.start=tk.IntVar(value=1)
        self.auto=tk.BooleanVar(value=True);self.overwrite=tk.BooleanVar(value=False)
        self.fps=tk.StringVar(value='24');self.quality=tk.StringVar(value='5');self.status=tk.StringVar(value='Choose a source folder and an SD card or staging folder.')
        self.controls=[]
        outer=ttk.Frame(root,padding=18);outer.pack(fill='both',expand=True)
        ttk.Label(outer,text='RoundTV Box Media Prep',font=('Helvetica',22,'bold')).pack(anchor='w')
        ttk.Label(outer,text='Prepare a whole folder for your round-screen TV. Originals stay untouched.').pack(anchor='w',pady=(3,15))
        paths=ttk.Frame(outer);paths.pack(fill='x');paths.columnconfigure(1,weight=1)
        for n,(label,var) in enumerate([('Video folder',self.source),('Output / SD root',self.dest)]):
            ttk.Label(paths,text=label).grid(row=n,column=0,sticky='w',padx=(0,12),pady=5)
            w=ttk.Entry(paths,textvariable=var);w.grid(row=n,column=1,sticky='ew');self.controls.append(w)
            w=ttk.Button(paths,text='Choose…',command=lambda v=var:self.choose(v));w.grid(row=n,column=2,padx=(8,0));self.controls.append(w)
        options=ttk.Frame(outer);options.pack(fill='x',pady=10)
        for label,var,lo,hi in [('Fallback season',self.season,1,99),('First episode',self.start,1,999)]:
            ttk.Label(options,text=label).pack(side='left',padx=(0,6));w=ttk.Spinbox(options,from_=lo,to=hi,textvariable=var,width=5);w.pack(side='left',padx=(0,16));self.controls.append(w)
        w=ttk.Checkbutton(options,text='Detect seasons / episode numbers from names',variable=self.auto);w.pack(side='left');self.controls.append(w)
        ttk.Label(outer,text='Recognizes S01E02, 1x02, Season 01 folders, and numbered filenames. Unnumbered files use natural order.').pack(anchor='w')
        toolbar=ttk.Frame(outer);toolbar.pack(fill='x',pady=(10,6))
        for title,cmd in [('Scan folder',self.scan),('Edit selected',self.edit),('Remove selected',self.remove)]:
            w=ttk.Button(toolbar,text=title,command=cmd);w.pack(side='left',padx=(0,8));self.controls.append(w)
        self.count=ttk.Label(toolbar,text='No videos scanned');self.count.pack(side='right')
        table=ttk.Frame(outer);table.pack(fill='both',expand=True)
        self.tree=ttk.Treeview(table,columns=('source','season','episode','output','status'),show='headings',selectmode='extended')
        for name,title,width in [('source','Source',235),('season','Season',55),('episode','Episode',60),('output','Output folder',340),('status','Status',145)]:
            self.tree.heading(name,text=title);self.tree.column(name,width=width,minwidth=45,stretch=name in ('source','output'))
        sy=ttk.Scrollbar(table,orient='vertical',command=self.tree.yview);sx=ttk.Scrollbar(table,orient='horizontal',command=self.tree.xview)
        self.tree.configure(yscrollcommand=sy.set,xscrollcommand=sx.set);self.tree.grid(row=0,column=0,sticky='nsew');sy.grid(row=0,column=1,sticky='ns');sx.grid(row=1,column=0,sticky='ew');table.rowconfigure(0,weight=1);table.columnconfigure(0,weight=1)
        self.tree.bind('<Double-1>',lambda e:self.edit())
        tuning=ttk.Frame(outer);tuning.pack(fill='x',pady=10)
        for label,var,values in [('FPS',self.fps,['15','20','24','25','30']),('Quality',self.quality,['2','4','5','7','10','14','20'])]:
            ttk.Label(tuning,text=label).pack(side='left',padx=(0,5));w=ttk.Combobox(tuning,textvariable=var,values=values,state='readonly',width=5);w.pack(side='left',padx=(0,12));self.controls.append(w)
        ttk.Label(tuning,text='Quality: lower = clearer / larger').pack(side='left')
        w=ttk.Checkbutton(tuning,text='Replace existing output files',variable=self.overwrite);w.pack(side='right');self.controls.append(w)
        ttk.Label(outer,textvariable=self.status,wraplength=1000).pack(anchor='w',pady=(0,5))
        self.filebar=ttk.Progressbar(outer,maximum=100);self.filebar.pack(fill='x',pady=3)
        self.batchbar=ttk.Progressbar(outer,maximum=100);self.batchbar.pack(fill='x',pady=3)
        footer=ttk.Frame(outer);footer.pack(fill='x',pady=(10,0))
        w=ttk.Button(footer,text='Convert folder',command=self.start_batch);w.pack(side='left');self.controls.append(w)
        self.cancel_btn=ttk.Button(footer,text='Cancel',command=self.cancel,state='disabled');self.cancel_btn.pack(side='left',padx=8)
        self.logbtn=ttk.Button(footer,text='Save report…',command=self.save_report);self.logbtn.pack(side='right')
        ttk.Label(footer,text='MJPEG AVI · 360×360 center crop · 48 kHz mono PCM').pack(side='right',padx=12)
        self.report=[];root.protocol('WM_DELETE_WINDOW',self.close);root.after(80,self.poll)
    def choose(self,var):
        value=filedialog.askdirectory(parent=self.root,title='Choose folder')
        if value:var.set(value)
    def set_busy(self,busy):
        self.busy=busy
        for w in self.controls:
            w.configure(state='disabled' if busy else ('readonly' if isinstance(w,ttk.Combobox) else 'normal'))
        self.cancel_btn.configure(state='normal' if busy else 'disabled')
    def refresh(self):
        self.tree.delete(*self.tree.get_children())
        for i,j in enumerate(self.jobs):
            try:source=str(j.source.relative_to(self.scanned_source))
            except (TypeError,ValueError):source=str(j.source)
            self.tree.insert('', 'end',iid=str(i),values=(source,j.season,j.episode,str(j.relative()),j.status))
        self.count.configure(text=f'{len(self.jobs)} videos')
    def scan(self):
        if self.busy:return
        try:
            root=Path(self.source.get()).expanduser().resolve()
            if not self.source.get().strip() or not root.is_dir():raise ValueError('Choose an existing video folder.')
            season,start=self.season.get(),self.start.get()
            if not 1<=season<=99 or not 1<=start<=999:raise ValueError('Season: 1–99. First episode: 1–999.')
        except (ValueError,tk.TclError) as e:messagebox.showerror('Check folder / numbering',str(e),parent=self.root);return
        auto=self.auto.get();self.engine=Engine();self.set_busy(True);self.status.set('Scanning video files, including subfolders…')
        def worker():
            try:self.events.put(('scanned',root,scan(root,season,start,auto,self.engine.cancelled)))
            except Cancelled:self.events.put(('scan_error','Scan cancelled.'))
            except Exception as e:self.events.put(('scan_error',str(e)))
        threading.Thread(target=worker,daemon=True).start()
    def edit(self):
        if self.busy:return
        selected=self.tree.selection()
        if not selected:return
        j=self.jobs[int(selected[0])];win=tk.Toplevel(self.root);win.title('Edit episode');win.transient(self.root);win.grab_set()
        values=[tk.StringVar(value=str(v)) for v in (j.season,j.episode,j.title)]
        for i,(label,var) in enumerate(zip(['Season','Episode','Title'],values)):
            ttk.Label(win,text=label).grid(row=i,column=0,padx=12,pady=8,sticky='w');ttk.Entry(win,textvariable=var,width=45).grid(row=i,column=1,padx=12,pady=8)
        def save():
            try:
                s,e=int(values[0].get()),int(values[1].get());title=values[2].get().strip()
                if not 1<=s<=99 or not 1<=e<=999 or not title:raise ValueError('Use season 1–99, episode 1–999, and a title.')
            except ValueError as ex:messagebox.showerror('Invalid episode',str(ex),parent=win);return
            j.season,j.episode,j.title,j.status=s,e,title,'Ready';self.refresh();win.destroy()
        ttk.Button(win,text='Save',command=save).grid(row=3,column=1,padx=12,pady=12,sticky='e')
    def remove(self):
        if self.busy:return
        remove={int(i) for i in self.tree.selection()};self.jobs=[j for i,j in enumerate(self.jobs) if i not in remove];self.refresh()
    def start_batch(self):
        if self.busy:return
        try:
            find_tools();check_plan(self.jobs)
            if not self.jobs:raise ValueError('Scan your video folder first.')
            source=Path(self.source.get()).expanduser().resolve();dest=Path(self.dest.get()).expanduser().resolve()
            if source!=self.scanned_source:raise ValueError('The source folder changed. Scan it again first.')
            if not self.dest.get().strip() or not dest.is_dir():raise ValueError('Choose an existing SD root or output folder.')
            if source==dest or source in dest.parents or dest in source.parents:raise ValueError('Choose separate source and output folders; neither can contain the other.')
            fps,quality=int(self.fps.get()),int(self.quality.get());overwrite=self.overwrite.get()
        except (ValueError,OSError) as e:messagebox.showerror('Cannot start',str(e),parent=self.root);return
        self.report=[f'Output: {dest}',f'FPS: {fps}; quality: {quality}; replace: {overwrite}'];self.filebar['value']=0;self.batchbar['value']=0
        self.engine=Engine(lambda v:self.events.put(('progress',v)));self.set_busy(True)
        jobs=list(self.jobs)
        def worker():
            counts={'Done':0,'Skipped':0,'Failed':0};cancelled=False
            for i,j in enumerate(jobs):
                if self.engine.cancelled.is_set():cancelled=True;break
                self.events.put(('current',i,f'Converting {i+1}/{len(jobs)}: {j.source.name}'))
                try:
                    result=self.engine.convert(j,dest,fps,quality,overwrite)
                    counts['Skipped' if result.startswith('Skipped') else 'Done']+=1;detail=result
                except Cancelled:
                    result='Cancelled';detail='Cancelled; incomplete output removed.';cancelled=True
                except Exception as e:result='Failed';detail=str(e);counts['Failed']+=1
                self.events.put(('result',i,result,detail,len(jobs)))
                if cancelled:break
            self.events.put(('finished',counts,cancelled))
        threading.Thread(target=worker,daemon=True).start()
    def cancel(self):
        if self.engine:self.engine.cancel()
        self.status.set('Cancelling… completed files will be kept.');self.cancel_btn.configure(state='disabled')
    def close(self):
        if self.busy:self.closing=True;self.cancel()
        else:self.root.destroy()
    def save_report(self):
        path=filedialog.asksaveasfilename(parent=self.root,defaultextension='.txt',initialfile='RoundTV-conversion-report.txt')
        if path:
            try:Path(path).write_text('\n'.join(self.report),encoding='utf-8')
            except OSError as e:messagebox.showerror('Cannot save report',str(e),parent=self.root)
    def poll(self):
        try:
            while True:
                event=self.events.get_nowait();kind=event[0]
                if kind=='scanned':
                    self.scanned_source,self.jobs=event[1:];self.refresh();self.set_busy(False)
                    self.status.set('Review numbering and output paths, then Convert folder. Existing outputs are skipped by default.' if self.jobs else 'No supported video files found.')
                elif kind=='scan_error':self.status.set(event[1]);self.set_busy(False)
                elif kind=='current':
                    i,text=event[1:];self.status.set(text);self.filebar['value']=0;self.tree.set(str(i),'status','Converting');self.tree.see(str(i))
                elif kind=='progress':self.filebar['value']=event[1]*100
                elif kind=='result':
                    i,result,detail,total=event[1:];self.jobs[i].status=result;self.tree.set(str(i),'status',result)
                    self.report.append(f'{self.jobs[i].source}\n  -> {self.jobs[i].relative()}\n  {detail}')
                    self.batchbar['value']=(i+1)/total*100
                    if result!='Cancelled':self.filebar['value']=100
                elif kind=='finished':
                    counts,cancelled=event[1:];self.set_busy(False)
                    self.status.set(f"{'Cancelled' if cancelled else 'Finished'} — {counts['Done']} converted, {counts['Skipped']} skipped, {counts['Failed']} failed. Save report for details.")
        except queue.Empty:pass
        if self.closing and not self.busy:self.root.destroy();return
        self.root.after(80,self.poll)

def launch():
    root=tk.Tk();App(root);root.mainloop()

if __name__=='__main__':launch()
