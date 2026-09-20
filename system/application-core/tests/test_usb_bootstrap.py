#!/usr/bin/env python3
"""Real derived cold bootstrap against isolated configfs attribute files."""
import importlib.util,os,subprocess,tempfile
from pathlib import Path
project=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location("derive",project/"system/application-core/scripts/derive-usb-bootstrap.py")
derive=importlib.util.module_from_spec(spec);spec.loader.exec_module(derive)
with tempfile.TemporaryDirectory(prefix="gkd-app-bootstrap-test.",dir="/tmp/gkd-mini-public") as d:
 root=Path(d);script=root/"bootstrap"
 script.write_text(derive.derive((project/"kernel/current/initramfs/gkd-recovery-usb").read_text()))
 (root/"udc/dwc2").mkdir(parents=True);(root/"disk").touch()
 hook=root/"hook";hook.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >>"'+str(root/"hook.log")+'"\n');hook.chmod(0o700)
 env=dict(os.environ,GKD_RECOVERY_USB_TESTING="1",GKD_RECOVERY_USB_TEST_ROOT=str(root),
  GKD_RECOVERY_USB_CONFIGFS=str(root/"configfs"),GKD_RECOVERY_USB_UDC_ROOT=str(root/"udc"),
  GKD_RECOVERY_USB_DEVICE=str(root/"disk"),GKD_RECOVERY_USB_SSH_HOOK=str(hook))
 run=lambda mode:subprocess.run(["sh",str(script),mode],env=env,capture_output=True,text=True,timeout=5)
 result=run("network-start");assert result.returncode==0,result
 gadget=root/"configfs/usb_gadget/gkd_recovery"
 assert (gadget/"UDC").read_text()=="dwc2"
 assert (gadget/"functions/rndis.usb0/dev_addr").read_text()=="02:00:00:00:64:01"
 assert not (gadget/"functions/mass_storage.0").exists()
 identity={str(f.relative_to(gadget)):f.read_bytes() for f in gadget.rglob("*") if f.is_file() and not f.is_symlink()}
 log=(root/"hook.log").read_bytes()
 assert run("network-start").returncode==1
 assert run("export-start").returncode==64
 assert (root/"hook.log").read_bytes()==log
 assert identity=={str(f.relative_to(gadget)):f.read_bytes() for f in gadget.rglob("*") if f.is_file() and not f.is_symlink()}
 assert "mass_storage" not in script.read_text()
print("GKD_APP_BOOTSTRAP=PASS cold-only/repeat-reject/identity-preserved/no-storage-api")
