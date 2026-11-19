from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
old='    invoke-virtual {v2, v0}, Lcom/android/server/net/NetworkPolicyManagerService;->bindNotificationManager(Landroid/app/INotificationManager;)V'
assert s.count(old)==1
s=s.replace(old,'    if-eqz v2, :kobo_no_network_policy\n'+old+'\n    :kobo_no_network_policy')
p.write_text(s)
print('Added null guard to notification binding when network policy service is disabled.')
