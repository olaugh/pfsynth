"""Small synthetic checks; no private GAPS assets required."""
import hashlib,json,tempfile,unittest,warnings
from pathlib import Path
from guitar_reference import reference_origin,fit_guard
class ReferenceTests(unittest.TestCase):
    def test_offsets_and_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);audio=p/'source.wav';audio.write_bytes(b'test audio identity');manifest=p/'offsets.json'
            manifest.write_text(json.dumps({'piece':{'audio_sha256':hashlib.sha256(audio.read_bytes()).hexdigest(),'audio_origin_seconds':.888,'method':'fixture'}}))
            offset,report=reference_origin(audio,2.636,'piece',manifest)
            self.assertEqual(offset,.888);self.assertEqual(report['midi_origin_seconds'],2.636);self.assertTrue(report['verified'])
            audio.write_bytes(b'different recording')
            with self.assertRaisesRegex(ValueError,'hash changed'):reference_origin(audio,2.636,'piece',manifest)
            with self.assertWarns(UserWarning):
                offset,report=reference_origin(audio,2.636,'unknown',manifest)
            self.assertEqual(offset,2.636);self.assertFalse(report['verified'])
    def test_candidate_gate(self):
        self.assertEqual(fit_guard([53,100,215],4.27,3.31)['listening_status'],'unaccepted')
        for values,a,b in [([100,0,80],5,3),([float('nan')],5,3),([1,2,4,100],5,3),([100]*4,5,6),([100],5,5),([100],float('nan'),2)]:
            with self.assertRaises(ValueError):fit_guard(values,a,b)
if __name__=='__main__':unittest.main()
