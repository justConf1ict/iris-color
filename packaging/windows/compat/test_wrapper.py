"""Static integrity/regression checks for the self-contained Windows x64 Fix1 EXE.

These checks do not claim to execute the installer under Windows.
"""
import pathlib, struct, unittest, zlib, hashlib, sys

MAGIC=b'IRIS07F1'

class TestCompatWrapper(unittest.TestCase):
    def test_final_footer_is_correct(self):
        if len(sys.argv) < 2: self.skipTest('wrapper path was not provided')
        data=pathlib.Path(sys.argv[1]).read_bytes()
        self.assertEqual(data[:2],b'MZ')
        pe=struct.unpack_from('<I',data,0x3c)[0]
        self.assertEqual(data[pe:pe+4],b'PE\0\0')
        self.assertEqual(struct.unpack_from('<H',data,pe+4)[0],0x8664)
        magic,size,crc=struct.unpack('<8sII',data[-16:])
        self.assertEqual(magic,MAGIC)
        self.assertGreater(size,500000)
        payload=data[-size-16:-16]
        self.assertEqual(zlib.crc32(payload),crc)
        self.assertEqual(hashlib.sha256(payload).hexdigest(),
            '896b40d6decd79281d49a1a6e66a2a096e9755a8e9eded8363fa2359bdd0ee4e')

if __name__=='__main__':
    argv=sys.argv[1:]
    sys.argv=[sys.argv[0]]+argv
    unittest.main(argv=[sys.argv[0]],verbosity=2)
