from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class OvernightSoakTests(unittest.TestCase):
    def exercise(self, mode):
        with tempfile.TemporaryDirectory(prefix='prime-soak-') as temporary:
            out=Path(temporary);snapshot=out/'snapshot';snapshot.mkdir()
            first=snapshot/'first'
            body={'success':'print("[==========] 7 tests from 1 test suite ran.")',
                  'failed':'raise SystemExit(7)',
                  'timeout':'import time; time.sleep(5)',
                  'tampered':'print("original")'}[mode]
            first.write_text('#!'+sys.executable+'\n'+body+'\n');first.chmod(0o700)
            second=snapshot/'second';second.write_text('#!'+sys.executable+'\nprint("second job")\n');second.chmod(0o700)
            jobs=[{'name':p.name,'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
                   'timeout_seconds':1 if mode=='timeout' else 3} for p in (first,second)]
            manifest=out/'manifest.json';manifest.write_text(json.dumps({'jobs':jobs}))
            if mode=='tampered':first.write_text('#!'+sys.executable+'\nprint("changed")\n')
            process=subprocess.run([sys.executable,str(ROOT/'tools/overnight_soak.py'),
                '--out',str(out),'--manifest',str(manifest),'--once','--duration-seconds','30',
                '--lock',str(out/'worker.lock')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=8)
            state=json.loads((out/'result.json').read_text()) if (out/'result.json').exists() else None
            if mode=='success':
                self.assertEqual(process.returncode,0,process.stdout)
                self.assertEqual(state['state'],'completed');self.assertEqual(state['jobs_passed'],2)
                self.assertEqual(state['gtest_case_executions_passed'],7)
                self.assertFalse(state['console_modified'])
            elif mode=='tampered':
                self.assertNotEqual(process.returncode,0)
                self.assertIn('Pinned executable changed',process.stdout)
                self.assertIsNone(state)
            else:
                self.assertNotEqual(process.returncode,0)
                self.assertEqual(state['state'],'failed');self.assertEqual(state['failed_job'],'first')
                self.assertEqual(state['jobs_passed'],0)
                self.assertEqual(state['failure']['result'],'timeout' if mode=='timeout' else 7)
                self.assertEqual(len((out/'journal.jsonl').read_text().splitlines()),1)
    def test_completion_and_case_accounting(self):self.exercise('success')
    def test_first_failure_stops_later_jobs(self):self.exercise('failed')
    def test_timeout_stops_later_jobs(self):self.exercise('timeout')
    def test_tampered_test_binary_is_refused(self):self.exercise('tampered')
if __name__=='__main__':unittest.main()
