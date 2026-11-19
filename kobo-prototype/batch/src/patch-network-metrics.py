from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
old='''    .line 58
    new-instance v0, Lcom/android/server/connectivity/DnsEventListenerService;'''
new='''    const-string/jumbo v0, "config.disable_network"
    const/4 v1, 0x0
    invoke-static {v0, v1}, Landroid/os/SystemProperties;->getBoolean(Ljava/lang/String;Z)Z
    move-result v0
    if-nez v0, :cond_27

'''+old
assert s.count(old)==1
p.write_text(s.replace(old,new))
print('Skip DNS network listener only when networking is disabled; metrics binder remains available.')
