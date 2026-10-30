"""Offline checks for the calibration ARM proof; no hardware/network access."""
from copy import deepcopy
from pathlib import Path
import hashlib,importlib.util,json,struct,subprocess,unittest,zlib

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('calibration_build',ROOT/'scripts/validate_calibration_build.py')
module=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)
BASE=Path('D:/FPGA_Project/SP2_Calibration_Build')

def rebind(manifest):
    data=json.dumps(manifest['binding'],ensure_ascii=True,sort_keys=True,separators=(',',':')).encode()
    manifest['id']=zlib.crc32(data)
    manifest['binding_sha256']=hashlib.sha256(data).hexdigest()
    return manifest

class CalibrationBuildTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest=json.loads((BASE/'calibration-profile-candidate.json').read_text())
        cls.status=json.loads((ROOT/'build/calibration-ui-firmware/freertos/build-status.json').read_text(encoding='utf-8-sig'))
        cls.build_run=Path(cls.status['run'])
        cls.commands=json.loads((cls.build_run/'cmake/compile_commands.json').read_text())
        cls.source=(cls.build_run/'ui/scope_calibration_profile.c').read_text()
        cls.elf=(ROOT/'build/calibration-ui-firmware/freertos/scope_lvgl_freertos.elf').read_bytes()

    def test_manifest_actual_data(self):
        channels=module.check_manifest(self.manifest)
        self.assertEqual([x['channel'] for x in channels],[1,2])
        self.assertEqual(len(channels[0]['dc_records']),33)

    def test_wrong_manifest_id(self):
        m=deepcopy(self.manifest);m['id']^=1
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_bad_binding_sha(self):
        m=deepcopy(self.manifest);m['binding_sha256']='0'*64
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_wrong_flags(self):
        m=deepcopy(self.manifest);m['flags']=0x101
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_duplicate_channels(self):
        m=deepcopy(self.manifest);m['binding']['channels'][1]['channel']=1;rebind(m)
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_changed_coefficient_even_with_new_crc(self):
        m=deepcopy(self.manifest);c=m['binding']['channels'][0]
        c['slope_v_per_code']=struct.unpack('<f',struct.pack('<f',c['slope_v_per_code']+.00001))[0]
        c['float32_le_hex']=struct.pack('<2f',c['slope_v_per_code'],c['offset_v']).hex();rebind(m)
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_bad_analysis_hash(self):
        m=deepcopy(self.manifest);m['binding']['channels'][0]['analysis_sha256']='0'*64;rebind(m)
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_bad_session_hash(self):
        m=deepcopy(self.manifest);m['binding']['channels'][1]['session_sha256']='0'*64;rebind(m)
        with self.assertRaises(AssertionError):module.check_manifest(m)

    def test_source_and_elf(self):
        expected=module.check_source(self.manifest,self.source)
        module.check_elf(self.elf,expected)

    def test_source_id_changed(self):
        text=self.source.replace(f"0x{self.manifest['id']:08x}",'0x00000001')
        with self.assertRaises(AssertionError):module.check_source(self.manifest,text)

    def test_source_coefficient_changed(self):
        text=self.source.replace('0.0093918182f','0.00949f')
        self.assertNotEqual(text,self.source)
        with self.assertRaises(AssertionError):module.check_source(self.manifest,text)

    def test_elf_missing_profile(self):
        expected=module.check_source(self.manifest,self.source)
        changed=self.elf.replace(expected,bytes(len(expected)))
        self.assertNotEqual(changed,self.elf)
        with self.assertRaises(AssertionError):module.check_elf(changed,expected)

    def test_actual_definitions_and_log(self):
        module.check_compile_commands(self.commands,self.build_run)
        module.check_compile_log((self.build_run/'compile.log').read_text())

    def test_disabled_macro(self):
        commands=deepcopy(self.commands)
        for command in commands:command['command']=command['command'].replace('-DSCOPE_ENGINEERING_CALIBRATION=1','-DSCOPE_ENGINEERING_CALIBRATION=0')
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_later_undefine_macro(self):
        commands=deepcopy(self.commands)
        for command in commands:command['command']+=' -USCOPE_ENGINEERING_CALIBRATION'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_missing_profile_translation_unit(self):
        commands=[c for c in self.commands if Path(c['file']).name!='scope_calibration_profile.c']
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_wrong_translation_unit_path(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_calibration_profile.c':
                command['file']='D:/Other/scope_calibration_profile.c'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_wrong_compile_directory(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_ui.c':command['directory']='D:/Other/cmake'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_wrong_actual_c_argument(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_calibration_profile.c':
                command['command']=command['command'].rsplit(' -c ',1)[0]+' -c D:/Other/scope_calibration_profile.c'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_duplicate_c_argument(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_calibration_profile.c':
                command['command']+=' -c '+subprocess.list2cmdline([command['file']])
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_missing_c_input(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_calibration_profile.c':
                command['command']=command['command'].rsplit(' -c ',1)[0]+' -c'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_extra_input_after_actual_c(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_calibration_profile.c':command['command']+=' D:/Other/extra.c'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_response_file_inside_option_value(self):
        for option in ('-I','-o'):
            with self.subTest(option=option):
                commands=deepcopy(self.commands)
                for command in commands:
                    if Path(command['file']).name=='scope_calibration_profile.c':
                        command['command']+=' '+option+' @D:/override.rsp'
                with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_shell_operator_inside_option_value(self):
        for option,operator in (('-I','&&'),('-o','|'),('-I','>'),('-o','&')):
            with self.subTest(option=option,operator=operator):
                commands=deepcopy(self.commands)
                for command in commands:
                    if Path(command['file']).name=='scope_calibration_profile.c':
                        command['command']+=' '+option+' '+operator
                with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_shell_operator_adjacent_to_option_value(self):
        commands=deepcopy(self.commands)
        for command in commands:
            if Path(command['file']).name=='scope_calibration_profile.c':
                command['command']+=' -I D:/Header&other'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_malformed_macro_value(self):
        commands=deepcopy(self.commands)
        for command in commands:
            command['command']=command['command'].replace('-DSCOPE_ENGINEERING_CALIBRATION=1','-DSCOPE_ENGINEERING_CALIBRATION=1xxx')
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_later_split_undefine(self):
        commands=deepcopy(self.commands)
        for command in commands:command['command']+=' -U SCOPE_ENGINEERING_CALIBRATION'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_later_split_disable(self):
        commands=deepcopy(self.commands)
        for command in commands:command['command']+=' -D SCOPE_ENGINEERING_CALIBRATION=0'
        with self.assertRaises(AssertionError):module.check_compile_commands(commands,self.build_run)

    def test_windows_argument_roundtrip(self):
        args=['D:\\Tools Path\\compiler.exe','-I','D:\\Header Path\\',
              '-DNAME="a b"','-c','D:\\Source Path\\scope_calibration_profile.c']
        self.assertEqual(module.windows_argv(subprocess.list2cmdline(args)),args)

    def test_quoted_compiler_source_and_output_paths(self):
        run=Path('D:/FPGA Project/proof path with spaces')
        compiler='D:/Xilinx Tools/bin/arm-none-eabi-gcc.exe'
        commands=[]
        for name in module.UNITS:
            source=run/('board' if name=='app.c' else 'ui')/name
            args=[compiler,'-DSCOPE_ENGINEERING_CALIBRATION=1','-mcpu=cortex-a9',
                  '-I','D:/Header Path/','-o',str(run/'cmake'/(name+'.obj')),'-c',str(source)]
            commands.append(dict(file=str(source),directory=str(run/'cmake'),command=subprocess.list2cmdline(args)))
        module.check_compile_commands(commands,run)

    def test_compile_warning_rejected(self):
        with self.assertRaises(AssertionError):module.check_compile_log('warning: pointer mismatch\nLinking C executable scope_lvgl_freertos.elf\n')

    def test_compile_error_rejected(self):
        with self.assertRaises(AssertionError):module.check_compile_log('error: invalid argument\nLinking C executable scope_lvgl_freertos.elf\n')

if __name__=='__main__':unittest.main()
