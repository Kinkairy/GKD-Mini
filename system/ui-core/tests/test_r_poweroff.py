from pathlib import Path
import subprocess
import hashlib
import tempfile
import unittest

LANE = Path(__file__).resolve().parents[1]

class PowerTest(unittest.TestCase):
    def test_shared_backend_is_exact_accepted_source(self):
        backend = LANE.parent / 'service-core/source/gkd-pmic-poweroff.c'
        self.assertEqual(hashlib.sha256(backend.read_bytes()).hexdigest(),
                         'd371b4c7b5a530dfff39e05ccfb21658f5512b7dffc7ba01798e13604f75ca5c')

    def test_cutoff_and_refusal_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            binary = base / 'test'
            subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Werror',
                            str(LANE / 'tests/test_r_poweroff.c'), '-o', str(binary)], check=True)
            for case in ('ok', 'probe', 'write', 'rw', 'swap', 'lun', 'busy', 'notram', 'missing', 'missing_i2c'):
                with self.subTest(case=case):
                    root = base / case
                    for path in ('proc/self', 'run/gkd-recovery', 'dev',
                                 'sys/kernel/config/usb_gadget/g/functions/mass_storage.usb0/lun.0'):
                        (root / path).mkdir(parents=True, exist_ok=True)
                    mounts = 'rootfs / rootfs rw 0 0\n'
                    if case == 'rw': mounts += '/dev/mmcblk1p1 /media/card vfat rw 0 0\n'
                    if case == 'notram': mounts = '/dev/mmcblk0p1 / ext4 ro 0 0\n'
                    (root / 'proc/self/mounts').write_text(mounts)
                    if case != 'missing':
                        (root / 'proc/swaps').write_text('Filename Type Size Used Priority\n' +
                            ('/dev/mmcblk0p3 partition 10 0 -1\n' if case == 'swap' else ''))
                    if case != 'missing_i2c': (root / 'dev/i2c-1').touch()
                    (root / 'sys/kernel/config/usb_gadget/g/functions/mass_storage.usb0/lun.0/file').write_text(
                        '/dev/mmcblk0\n' if case == 'lun' else '\n')
                    lock = root / 'run/gkd-recovery/card-operation.lock'
                    if case == 'busy': lock.mkdir()
                    subprocess.run([str(binary), str(root), '1' if case in ('ok', 'write') else '0',
                                    str(int(case == 'probe')), str(int(case == 'write'))], check=True)
                    self.assertEqual(lock.exists(), case == 'busy')

    def test_ui_does_not_halt_or_stop_network_on_r_cutoff(self):
        text = (LANE / 'source/gkd-recovery-ui.c').read_text()
        branch = text.split('case 4:', 1)[1].split('#else', 1)[0]
        self.assertIn('gkd_r_poweroff()', branch)
        self.assertNotIn('run_program', branch)
        self.assertNotIn('reboot(', branch)

if __name__ == '__main__':
    unittest.main()
