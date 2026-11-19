from pathlib import Path
import shutil,tarfile,re
base=Path(__file__).resolve().parents[1]
repo=base.parents[1]
root=base/'build/root'
if root.exists():shutil.rmtree(root)
shutil.copytree(repo/'android7-prototype/rootfs',root,symlinks=True)
shutil.copy2(repo/'kobo-prototype/build/init-kobo-compat',root/'init-kobo-compat')
# Keep only trusted, offscreen daemons. No network manager, vold, lmkd, USB or display device access.
keep={'vold.rc','logd.rc','servicemanager.rc','installd.rc','surfaceflinger.rc','keystore.rc','gatekeeperd.rc','audioserver.rc','mediacodec.rc','mediaextractor.rc','mediaserver.rc','debuggerd.rc'}
for p in (root/'kdinit').glob('*'):
 if p.name not in keep:p.unlink();continue
 lines=p.read_text().splitlines();out=[]
 for line in lines:
  if line.strip()=='critical' or line.strip().startswith('onrestart ') or line.strip().startswith('writepid '):continue
  out.append(line)
  if line.startswith('service ') and not line.startswith('service logd ') and not line.startswith('service servicemanager ') and not line.startswith('service vold '):out.append('    disabled')
 p.write_text('\n'.join(out)+'\n')
p=root/'init.rc';out=[]
for line in p.read_text().splitlines():
 s=line.strip()
 if s.startswith('import /init.usb'):continue
 if s=='init_user0' or s.startswith('ifup ') or s.startswith('setprop selinux.reload_policy '):continue
 if s.startswith(('mount ','mount_all ','swapon ','hostname ','domainname ')):continue
 if s.startswith(('write /proc/','write /sys/','chmod ' ,'chown ')) and (' /proc/' in ' '+s or ' /sys/' in ' '+s):continue
 out.append(line)
p.write_text('\n'.join(out)+'\n')
p=root/'init.zygote32.rc';s=p.read_text();s=s[:s.index('    onrestart')] if '    onrestart' in s else s
s=s.replace('    class main','    class main\n    disabled');p.write_text(s+'\n')
p=root/'init.kindle.rc';s=p.read_text().replace('/kdlib/libkdashmem.so:/kdlib/libkdnonwakealarm.so','/kdlib/libkdnonwakealarm.so');p.write_text(s)
props={}
for line in (root/'default.prop').read_text().splitlines():
 if '=' in line and not line.lstrip().startswith('#'):
  k,v=line.split('=',1);props[k]=v
props.update({'ro.kindleroid.fb.width':'600','ro.kindleroid.fb.height':'800','ro.kindleroid.fb.dpi':'160','ro.sf.lcd_density':'160','ro.kindleroid.fb.fps':'5','config.disable_network':'true','config.disable_storage':'false','config.disable_bluetooth':'true','config.disable_location':'true','config.disable_systemui':'true','config.disable_networktime':'true','config.disable_samplingprof':'true','ro.logd.kernel':'false','ro.logd.auditd':'false','persist.sys.usb.config':'none','dalvik.vm.dex2oat-flags':'--runtime-arg -Xgc:SS'})
(root/'fstab.kindle').write_text('/dev/block/kobo-data-image /data ext4 noatime,nosuid,nodev wait\n')
(root/'default.prop').write_text(''.join(k+'='+v+'\n' for k,v in props.items()))
shutil.copy2(repo/'android7-prototype/build/binder_echo',root/'kdbin/binder_echo')
shutil.copy2(base/'package/probe.jar',root/'kdbin/kobo-probe.jar')
shutil.copy2(base/'package/kobo-probe.apk',root/'kdbin/kobo-probe.apk')
with tarfile.open(base/'package/root.tar.gz','w:gz') as t:
 for p in sorted(root.iterdir()):t.add(p,arcname=p.name)
print('Batch root assembled')
