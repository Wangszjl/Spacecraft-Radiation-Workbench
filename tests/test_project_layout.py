import unittest
from pathlib import Path
R=Path(__file__).resolve().parents[1]
class Layout(unittest.TestCase):
 def test_independent_cmake(self):
  self.assertTrue((R/'CMakeLists.txt').exists(),'Independent CMake project is missing')
  s=(R/'CMakeLists.txt').read_text();self.assertIn('find_package(Geant4',s);self.assertNotIn('tkmaster_ui',s);self.assertNotIn('load_config',s)
 def test_no_duplicate_host_namespace(self):
  s=(R/'include/spacerad/Project.h').read_text()
  self.assertNotIn('scdat::radiation',s,'Private engine namespace must not collide with host')
 def test_public_embedding_contract(self):
  self.assertTrue((R/'include/spacerad/AnalysisService.h').exists(),'Public service is missing')
  self.assertTrue((R/'include/spacerad/WorkbenchWidget.h').exists(),'Embeddable widget is missing')
if __name__=='__main__':unittest.main()
