from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from verify_secondary import Mips


class OracleTests(unittest.TestCase):
    def program(self, words):
        cpu = Mips()
        for i, word in enumerate(words):
            cpu.write(0x80010000 + i * 4, word)
        return cpu

    def test_load_and_taken_branch_delay(self):
        cpu = self.program([0x24080007, 0x8c080200, 0x01004821, 0x01005021,
                            0x10000002, 0x240b0003, 0x240b0009, 0x03e00008, 0])
        cpu.write(0x200, 42)
        registers = cpu.call(0x80010000)
        self.assertEqual((registers[9], registers[10], registers[11]), (7, 42, 3))

    def test_register_write_supersedes_pending_load(self):
        cpu = self.program([0x8c080200, 0x24080005, 0x01004821, 0x03e00008, 0])
        cpu.write(0x200, 42)
        self.assertEqual(cpu.call(0x80010000)[9], 5)

    def test_unmapped_access_and_opcode_fail(self):
        cpu = self.program([0xffffffff])
        with self.assertRaises(ValueError): cpu.read(0x80010001)
        with self.assertRaises(ValueError): cpu.write(0x80200000, 1)
        with self.assertRaises(ValueError): cpu.call(0x80010000)


if __name__ == '__main__':
    unittest.main()
