from pathlib import Path
import sys,hashlib,json
p=Path(sys.argv[1]);s=p.read_text()
start=s.index('.method public getSyncAdapterPackagesForAuthorityAsUser(Ljava/lang/String;I)[Ljava/lang/String;')
end=s.index('.end method',start)+len('.end method')
method=s[start:end]
old='''    .local v2, "syncManager":Lcom/android/server/content/SyncManager;
    invoke-virtual {v2, p1, p2}, Lcom/android/server/content/SyncManager;->getSyncAdapterPackagesForAuthorityAsUser(Ljava/lang/String;I)[Ljava/lang/String;'''
new='''    .local v2, "syncManager":Lcom/android/server/content/SyncManager;
    # getSyncManager() intentionally returns null when config.disable_network is true.
    if-nez v2, :kobo_sync_manager_present
    const/4 v3, 0x0
    new-array v3, v3, [Ljava/lang/String;
    goto :kobo_restore_identity
    :kobo_sync_manager_present
    invoke-virtual {v2, p1, p2}, Lcom/android/server/content/SyncManager;->getSyncAdapterPackagesForAuthorityAsUser(Ljava/lang/String;I)[Ljava/lang/String;'''
assert method.count(old)==1
method=method.replace(old,new)
old2='''    .line 687
    invoke-static {v0, v1}, Lcom/android/server/content/ContentService;->restoreCallingIdentity(J)V'''
assert method.count(old2)==2
method=method.replace(old2,'    :kobo_restore_identity\n'+old2,1)
p.write_text(s[:start]+method+s[end:])
print('Patched only getSyncAdapterPackagesForAuthorityAsUser; cross-user permission and identity restoration retained.')
