import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATH = ROOT / 'scripts/audit_storage_platform.py'

class StoragePlatformTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATH.is_file(), 'storage platform auditor is required')
        spec = importlib.util.spec_from_file_location('storage_audit', PATH)
        self.audit = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.audit)

    def test_sd_mio_changes_allowed(self):
        old = {'ps7_mio_init_data_1_0': [('EMIT_MASKWRITE', (0xf8000704,0x3fff,0x1602))]}
        new = {'ps7_mio_init_data_1_0': old['ps7_mio_init_data_1_0'] + [('EMIT_MASKWRITE',(0xf80007a0,0x3fff,0x2680))]}
        self.assertEqual(len(self.audit.compare_operations(old,new)),1)

    def test_sd_clock_added_with_disabled_sd1_is_valid(self):
        old={"ps7_clock_init_data_1_0": []}
        new={"ps7_clock_init_data_1_0": [("EMIT_MASKWRITE",(0xf8000150,0x3f33,0x1001))]}
        self.assertEqual(len(self.audit.compare_operations(old,new)),1)

    def test_cd47_selector_allowed(self):
        self.assertEqual(len(self.audit.compare_operations({"ps7_mio_init_data_1_0": []},{"ps7_mio_init_data_1_0": [("EMIT_MASKWRITE",(0xf8000830,0x3f003f,0x2f0037))]})),1)

    def test_usb_reset_mio46_rejected(self):
        old={'ps7_mio_init_data_1_0': []}
        new={'ps7_mio_init_data_1_0': [('EMIT_MASKWRITE',(0xf80007b8,0x3fff,0x2680))]}
        with self.assertRaises(ValueError): self.audit.compare_operations(old,new)

    def test_qspi_removal_rejected(self):
        old={'ps7_mio_init_data_1_0': [('EMIT_MASKWRITE',(0xf8000704,0x3fff,0x1602))]}
        with self.assertRaises(ValueError): self.audit.compare_operations(old,{'ps7_mio_init_data_1_0':[]})

    def test_unrelated_aper_clock_bit_rejected(self):
        old={'ps7_clock_init_data_1_0':[('EMIT_MASKWRITE',(0xf800012c,0x01ffcccd,0x01ec000d))]}
        new={'ps7_clock_init_data_1_0':[('EMIT_MASKWRITE',(0xf800012c,0x01ffcccd,0x01ec400c))]}
        with self.assertRaises(ValueError): self.audit.compare_operations(old,new)

    def test_pl_topology_mutation_rejected(self):
        with self.assertRaises(ValueError): self.audit.compare_bd({'components':{'adc':{'clock':50}}},{'components':{'adc':{'clock':25}}})

    def test_hwh_connected_port_change_rejected(self):
        old=b"<EDKSYSTEM><MODULES><MODULE INSTANCE=\"adc\"><PORTS><PORT NAME=\"clk\" CLKFREQUENCY=\"50\"><CONNECTIONS><CONNECTION INSTANCE=\"ps7\" PORT=\"FCLK_CLK0\"/></CONNECTIONS></PORT></PORTS></MODULE></MODULES></EDKSYSTEM>"
        with self.assertRaises(ValueError): self.audit.compare_original_hwh(old,old.replace(b"50",b"25"))

    def test_uart_output_is_bounded(self):
        source=(ROOT/"firmware/storage/readonly_probe.c").read_text()
        self.assertIn("UART_POLL_LIMIT",source)
        self.assertIn("void outbyte(char c)",source)
        self.assertNotIn("XUartPs_SendByte(",source)

    def test_readonly_probe_has_write_guard(self):
        source=(ROOT/'firmware/storage/readonly_probe.c').read_text(encoding='utf-8')
        self.assertIn('return RES_WRPRT;',source)
        self.assertNotIn('XSdPs_WritePolled(',source)
        self.assertNotIn('f_getfree(',source)
        self.assertNotIn('f_open(',source)
        self.assertIn('PROBE_TIMEOUT_SECONDS',source)

if __name__=='__main__': unittest.main()
