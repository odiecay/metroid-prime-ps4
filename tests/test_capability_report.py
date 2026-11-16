import copy
import importlib.util
import json
from pathlib import Path
import unittest
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('compat', ROOT/'tools/compatibility_report.py')
compat=importlib.util.module_from_spec(spec); spec.loader.exec_module(compat)

class CapabilityReportTests(unittest.TestCase):
    def setUp(self):
        self.req=json.loads((ROOT/'renderer/requirements.json').read_text())
        self.rows=[{'schema':1,'target':'orbis'},
          {'device_name':'synthetic fixture, not a device run', 'api_version':(1<<22)|(1<<12),
           'features':{f:True for f in self.req['required_features']+self.req['core_features']+['textureCompressionBC']},
           'limits':self.req['minimum_limits'].copy()},
          {'instance_extensions':self.req['required_instance_extensions']},
          {'device_extensions':self.req['required_device_extensions']},
          {'stage':'transfer_readback','passed':True}, {'stage':'probe_end','passed':True}]
    def test_baseline_never_promotes_to_dawn_or_gameplay(self):
        r=compat.evaluate(self.rows)
        self.assertEqual(r['stage'],'baseline_passed')
        self.assertFalse(r['dawn_device_tested']); self.assertFalse(r['aurora_rendered'])
        self.assertFalse(r['gameplay_tested']); self.assertFalse(r['release_accepted'])
    def test_each_missing_feature_is_reported(self):
        for feature in self.req['required_features']:
            with self.subTest(feature=feature):
                rows=copy.deepcopy(self.rows); rows[1]['features'][feature]=False
                self.assertIn('feature:'+feature,compat.evaluate(rows)['baseline_failures'])
    def test_no_inference_from_incomplete_log(self):
        self.assertEqual(compat.evaluate(self.rows[:-1])['stage'],'baseline_failed')
    def test_core_feature_requirements_are_not_silently_downgraded(self):
        for feature in self.req['core_features']:
            with self.subTest(feature=feature):
                rows=copy.deepcopy(self.rows);rows[1]['features'][feature]=False
                self.assertIn('core_feature:'+feature,compat.evaluate(rows)['baseline_failures'])
    def test_host_does_not_count_as_console(self):
        self.rows[0]['target']='linux'
        self.assertEqual(compat.evaluate(self.rows)['stage'],'baseline_failed')
    def test_extensions_and_limits_are_real_requirements(self):
        self.rows[2]['instance_extensions']=[]
        self.rows[1]['limits']['maxImageDimension2D']=1024
        r=compat.evaluate(self.rows)
        self.assertIn('extension:VK_EXT_headless_surface',r['baseline_failures'])
        self.assertIn('limit:maxImageDimension2D',r['baseline_failures'])
    def test_missing_readback_does_not_count_as_gpu_execution(self):
        self.rows[4]['passed']=False
        self.assertEqual(compat.evaluate(self.rows)['stage'],'baseline_failed')

if __name__=='__main__': unittest.main()
