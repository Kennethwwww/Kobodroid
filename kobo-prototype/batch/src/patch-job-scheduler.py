from pathlib import Path
import sys
p=Path(sys.argv[1]);s=p.read_text()
old='''    iget-object v0, p0, Lcom/android/server/job/JobSchedulerService;->mControllers:Ljava/util/List;

    invoke-static {p0}, Lcom/android/server/job/controllers/ConnectivityController;->get(Lcom/android/server/job/JobSchedulerService;)Lcom/android/server/job/controllers/ConnectivityController;

    move-result-object v1

    invoke-interface {v0, v1}, Ljava/util/List;->add(Ljava/lang/Object;)Z'''
assert s.count(old)==1
new='''    const-string/jumbo v0, "config.disable_network"
    const/4 v1, 0x0
    invoke-static {v0, v1}, Landroid/os/SystemProperties;->getBoolean(Ljava/lang/String;Z)Z
    move-result v0
    if-nez v0, :kobo_skip_disabled_network_controller
'''+old+'''\n    :kobo_skip_disabled_network_controller'''
p.write_text(s.replace(old,new))
print('Keep network-constrained jobs unsatisfied when networking is disabled; other controllers unchanged.')
