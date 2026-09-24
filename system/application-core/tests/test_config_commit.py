#!/usr/bin/env python3
"""Real C transaction + real shell compiler on temporary files; block identity alone is a fixture."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / "system/application-core"
CORE = ROOT / "system/config-core"

@unittest.skipUnless(os.geteuid() == 0, "run persistence fixtures in an isolated root container")
class ConfigCommit(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="gkd-real-commit-")
        cls.work = Path(cls.temporary.name)
        cls.data = cls.work / "data"
        cls.schema = cls.work / "application.schema"
        subprocess.run(["python3", str(APP / "scripts/derive-schema.py"), str(CORE / "schema/gdkmini.schema"), str(cls.schema)], check=True, capture_output=True)
        cls.command = cls.work / "config-command"
        cls.command.write_text('#!/bin/sh\nif [ "$1" = commit ] && [ "${GKD_TEST_FAIL-0}" = 1 ] && [ "$2" != "$GKD_TEST_BASE" ]; then\n export GKD_CONFIG_FAILPOINT=after_run_switch\nelse\n unset GKD_CONFIG_FAILPOINT\nfi\nexec '+str(CORE / 'device/gkd-config')+' "$@"\n')
        cls.command.chmod(0o700)
        defines = {"GKD_APP_CONFIG_RUNTIME": cls.data / "runtime", "GKD_APP_CONFIG_EMBEDDED": cls.data / "embedded.conf",
            "GKD_APP_CONFIG_P2": cls.data / "fake-block", "GKD_APP_CONFIG_EXEC": cls.command,
            "GKD_APP_CONFIG_GUARD": "/bin/false", "GKD_APP_CONFIG_CORE_RUN": cls.data / "run",
            "GKD_APP_CONFIG_CORE_STATE": cls.data / "state", "TEST_ETC": cls.data / "etc"}
        fixture = cls.work / "commit.c"
        fixture.write_text('#define _GNU_SOURCE\n'+''.join('#define '+key+' '+json.dumps(str(value))+'\n' for key,value in defines.items())+'''
#define main original_config_store_main
#include '''+json.dumps(str(APP / "source/gkd-app-config-store.c"))+'''
#undef main
int __real_stat(const char *,struct stat *);
int __wrap_stat(const char *path,struct stat *st){
 if(strcmp(path,GKD_APP_CONFIG_P2))return __real_stat(path,st);
 struct stat directory;if(__real_stat(TEST_ETC,&directory))return -1;
 memset(st,0,sizeof(*st));st->st_mode=S_IFBLK|0600;st->st_rdev=directory.st_dev;return 0;
}
int main(int argc,char **argv){
 if(argc!=2)return 2;
 int fd=open(TEST_ETC,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);if(fd<0)return 2;
 char generation[65]={0};
 enum settings_result result=settings_commit(fd,argv[1],NULL,config_command_real,NULL,generation);
 int saved=errno;close(fd);errno=saved;return report_settings(result,generation);
}
''')
        cls.binary = cls.work / "commit"
        flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-no-pie"] if os.environ.get("GKD_TEST_SANITIZE") == "1" else ["-O2"]
        subprocess.run(["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror", "-I"+str(APP / "include"),
            "-I"+str(ROOT / "system/ui-core/include")]+flags+[str(fixture), str(APP / "source/gkd-app-job.c"),
            "-Wl,--wrap=stat", "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls): cls.temporary.cleanup()

    def initialize(self, persistent=True):
        if self.data.exists(): shutil.rmtree(self.data)
        self.data.mkdir(mode=0o700)
        for name in ("runtime", "etc"): (self.data / name).mkdir(mode=0o700)
        self.old = b"usb_frontend_ready_timeout_ms=60000\n"
        self.proposal = self.data / "runtime/config.override.conf"
        self.proposal.write_bytes(self.old)
        (self.data / "embedded.conf").write_bytes(self.old)
        self.persist = self.data / "etc/gkd-mini/gdkmini.override.conf"
        if persistent:
            self.persist.parent.mkdir(mode=0o700); self.persist.write_bytes(self.old)
        self.env = dict(os.environ, GKD_CONFIG_SCHEMA=str(self.schema), GKD_CONFIG_MERGER=str(CORE / "device/gkd-config-merge.awk"),
            GKD_CONFIG_OVERRIDE=str(self.proposal), GKD_CONFIG_RUN_DIR=str(self.data / "run"), GKD_CONFIG_STATE_DIR=str(self.data / "state"),
            GKD_TEST_FAIL="0", ASAN_OPTIONS="detect_leaks=1:abort_on_error=1", UBSAN_OPTIONS="halt_on_error=1")
        applied = subprocess.run(["sh", str(CORE / "device/gkd-config"), "apply"], env=self.env, capture_output=True, text=True)
        self.assertEqual(applied.returncode, 0, applied.stderr)
        self.generation = (self.data / "run/current/generation").read_text().strip()
        self.env["GKD_TEST_BASE"] = self.generation

    def assert_generation(self, expected):
        for name in ("run", "state"):
            self.assertEqual((self.data / name / "current/generation").read_text().strip(), expected)

    def invoke(self, expected=None):
        return subprocess.run([str(self.binary), expected or self.generation], env=self.env, text=True, capture_output=True, timeout=15)

    def test_success_including_first_save(self):
        for exists in (False, True):
            with self.subTest(persistent=exists):
                self.initialize(exists)
                candidate = self.old + b"screenshot_hotkey=NONE\n"
                self.proposal.write_bytes(candidate)
                result = self.invoke()
                self.assertEqual(result.returncode, 0, result.stderr+result.stdout)
                self.assertIn("GKD_APP_SETTINGS=SAVED generation=", result.stdout)
                self.assertEqual(self.persist.read_bytes(), candidate)
                self.assertEqual(self.proposal.read_bytes(), candidate)
                new_generation = (self.data / "run/current/generation").read_text().strip()
                self.assertNotEqual(new_generation, self.generation)
                self.assert_generation(new_generation)

    def test_invalid_candidates_do_not_publish(self):
        for item in (b"screenshot_hotkey=L1+L2\n", b"screenshot_output_dir=/tmp/shots\n", b"input_map_a=KEY_UP\n"):
            with self.subTest(candidate=item):
                self.initialize(); self.proposal.write_bytes(self.old+item)
                result = self.invoke()
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("state=recoverable", result.stdout)
                self.assert_generation(self.generation)
                self.assertEqual(self.persist.read_bytes(), self.old)

    def test_failure_after_runtime_switch_restores_both(self):
        self.initialize();self.proposal.write_bytes(self.old+b"screenshot_hotkey=NONE\n")
        self.env["GKD_TEST_FAIL"]="1"
        result=self.invoke()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("state=recoverable", result.stdout)
        self.assert_generation(self.generation)
        self.assertEqual(self.persist.read_bytes(), self.old)
        self.assertEqual(self.proposal.read_bytes(), self.old)

    def test_stale_generation_cannot_overwrite(self):
        self.initialize();self.proposal.write_bytes(self.old+b"screenshot_hotkey=NONE\n")
        result=self.invoke("0"*64)
        self.assertNotEqual(result.returncode, 0)
        self.assert_generation(self.generation)
        self.assertEqual(self.persist.read_bytes(), self.old)

if __name__ == "__main__": unittest.main()
