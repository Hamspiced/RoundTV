import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import tempfile,unittest,subprocess,hashlib
from batch import scan,check_plan,Job,Engine,Cancelled
from prepare_media import validate_avi
class Tests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.source=self.root/'input';self.source.mkdir();self.out=self.root/'output';self.out.mkdir()
 def tearDown(self):self.tmp.cleanup()
 def touch(self,name):
  p=self.source/name;p.parent.mkdir(parents=True,exist_ok=True);p.touch();return p
 def test_numbering(self):
  self.touch('Season 02/10 - Ten.mp4');self.touch('Season 02/2 - Two.mkv');self.touch('Show_S01E03_Title.mp4');self.touch('Season 02/Other.mov');self.touch('.ignore/S03E01.mp4')
  jobs=scan(self.source);check_plan(jobs)
  self.assertEqual({(j.season,j.episode) for j in jobs},{(2,10),(2,2),(1,3),(2,1)})
 def test_natural_and_manual(self):
  self.touch('clip10.mp4');self.touch('clip2.mp4');self.touch('clip1.mp4')
  jobs=scan(self.source,4,6,False)
  self.assertEqual([(j.source.stem,j.season,j.episode) for j in jobs],[('clip1',4,6),('clip2',4,7),('clip10',4,8)])
 def test_duplicate_detection(self):
  self.touch('One.S01E01.mp4');self.touch('Two.S01E01.mp4')
  with self.assertRaises(ValueError):check_plan(scan(self.source))
 def test_collision_reserved_numbers(self):
  self.touch('a.mp4');self.touch('z.S01E01.mp4')
  jobs=scan(self.source);check_plan(jobs);self.assertEqual([j.episode for j in jobs],[2,1])
 def test_real_conversion_skip_and_overwrite(self):
  p=self.source/'S02E03 - test.mp4'
  subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=320x240:rate=20','-t','1','-c:v','mpeg4',str(p)],check=True)
  before=hashlib.sha256(p.read_bytes()).digest();j=scan(self.source)[0];updates=[]
  self.assertEqual(Engine(updates.append).convert(j,self.out),'Done');output=self.out/j.relative();validate_avi(output)
  first=hashlib.sha256(output.read_bytes()).digest()
  self.assertEqual(Engine().convert(j,self.out),'Skipped (exists)');self.assertEqual(first,hashlib.sha256(output.read_bytes()).digest())
  self.assertEqual(Engine().convert(j,self.out,overwrite=True),'Done')
  self.assertEqual(before,hashlib.sha256(p.read_bytes()).digest());self.assertTrue(updates)
 def test_cancel_active_conversion(self):
  p=self.source/'long.mp4'
  subprocess.run(['ffmpeg','-v','error','-y','-f','lavfi','-i','testsrc2=size=320x240:rate=20','-t','8','-c:v','mpeg4',str(p)],check=True)
  j=Job(p,1,1,'Long');e=Engine();e.progress=lambda v:e.cancel()
  with self.assertRaises(Cancelled):e.convert(j,self.out)
  self.assertFalse((self.out/j.relative()).exists());self.assertFalse(list(self.out.rglob('.roundtv-*')))
 def test_bad_input_cleanup(self):
  j=Job(self.touch('broken.mp4'),1,1,'Broken')
  with self.assertRaises(ValueError):Engine().convert(j,self.out)
  self.assertFalse((self.out/j.relative()).exists())
if __name__=='__main__':unittest.main(verbosity=2)
