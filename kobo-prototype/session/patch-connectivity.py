"""Publish a connectivity Binder when the real service is absent.

The Kobo guest shares the host network stack without netd, so it reports one connected Wi-Fi
network (apps such as WeRead download books only on Wi-Fi); patch-wifi.py answers for WifiManager. Everything else keeps the inert defaults."""
from pathlib import Path
import re
import sys

root, interface = map(Path, sys.argv[1:])
server = root / "com/android/server/SystemServer.smali"
stub = "Landroid/net/IConnectivityManager$Stub;"
klass = "Lcom/android/server/KoboHostConnectivity;"
methods = re.findall(r"^\.method public abstract (\S+)$", interface.read_text(), re.M)
assert "getActiveNetworkInfo()Landroid/net/NetworkInfo;" in methods
body = f""".class public final {klass}
.super {stub}

.method public constructor <init>()V
    .locals 0
    invoke-direct {{p0}}, {stub}-><init>()V
    return-void
.end method

.method public static publishIfMissing()V
    .locals 2
    const-string v0, "connectivity"
    invoke-static {{v0}}, Landroid/os/ServiceManager;->checkService(Ljava/lang/String;)Landroid/os/IBinder;
    move-result-object v1
    if-nez v1, :done
    new-instance v1, {klass}
    invoke-direct {{v1}}, {klass}-><init>()V
    invoke-static {{v0, v1}}, Landroid/os/ServiceManager;->addService(Ljava/lang/String;Landroid/os/IBinder;)V
    :done
    return-void
.end method
"""
link = "invoke-static {}, " + klass + "->ethernet()Landroid/net/NetworkInfo;\n    move-result-object v0\n    return-object v0"
network = "new-instance v0, Landroid/net/Network;\n    const/16 v1, 0x64\n    invoke-direct {v0, v1}, Landroid/net/Network;-><init>(I)V\n    return-object v0"
special = {
    "getActiveNetworkInfo()Landroid/net/NetworkInfo;": link,
    "getActiveNetworkInfoForUid(IZ)Landroid/net/NetworkInfo;": link,
    "getNetworkInfoForUid(Landroid/net/Network;IZ)Landroid/net/NetworkInfo;": link,
    "getNetworkInfo(I)Landroid/net/NetworkInfo;": "const/4 v0, 0x1\n    if-eq p1, v0, :link\n    const/4 v0, 0x0\n    return-object v0\n    :link\n    " + link,
    "getAllNetworkInfo()[Landroid/net/NetworkInfo;": "const/4 v0, 0x1\n    new-array v0, v0, [Landroid/net/NetworkInfo;\n    invoke-static {}, " + klass + "->ethernet()Landroid/net/NetworkInfo;\n    move-result-object v1\n    const/4 p0, 0x0\n    aput-object v1, v0, p0\n    return-object v0",
    "getActiveNetwork()Landroid/net/Network;": network,
    "getActiveNetworkForUid(IZ)Landroid/net/Network;": network,
    "getAllNetworks()[Landroid/net/Network;": "const/4 v0, 0x1\n    new-array v0, v0, [Landroid/net/Network;\n    new-instance v1, Landroid/net/Network;\n    const/16 p0, 0x64\n    invoke-direct {v1, p0}, Landroid/net/Network;-><init>(I)V\n    const/4 p0, 0x0\n    aput-object v1, v0, p0\n    return-object v0",
    "getNetworkCapabilities(Landroid/net/Network;)Landroid/net/NetworkCapabilities;": "new-instance v0, Landroid/net/NetworkCapabilities;\n    invoke-direct {v0}, Landroid/net/NetworkCapabilities;-><init>()V\n    const/4 v1, 0x1\n    invoke-virtual {v0, v1}, Landroid/net/NetworkCapabilities;->addTransportType(I)Landroid/net/NetworkCapabilities;\n"
        + "".join(f"    const/16 v1, {hex(c)}\n    invoke-virtual {{v0, v1}}, Landroid/net/NetworkCapabilities;->addCapability(I)Landroid/net/NetworkCapabilities;\n" for c in (11, 12, 13, 14, 15, 16))
        + "    return-object v0",
}
request = "invoke-static {p1}, " + klass + "->request(Landroid/net/NetworkCapabilities;)Landroid/net/NetworkRequest;\n    move-result-object v0\n    return-object v0"
special["listenForNetwork(Landroid/net/NetworkCapabilities;Landroid/os/Messenger;Landroid/os/IBinder;)Landroid/net/NetworkRequest;"] = request
special["requestNetwork(Landroid/net/NetworkCapabilities;Landroid/os/Messenger;ILandroid/os/IBinder;I)Landroid/net/NetworkRequest;"] = request
special["pendingRequestForNetwork(Landroid/net/NetworkCapabilities;Landroid/app/PendingIntent;)Landroid/net/NetworkRequest;"] = request
assert set(special) <= set(methods)
body += f"""
.field private static nextRequest:I

# Callbacks never fire, but each registration gets a distinct valid request id.
.method private static request(Landroid/net/NetworkCapabilities;)Landroid/net/NetworkRequest;
    .locals 3
    if-nez p0, :caps
    new-instance p0, Landroid/net/NetworkCapabilities;
    invoke-direct {{p0}}, Landroid/net/NetworkCapabilities;-><init>()V
    :caps
    sget v1, {klass}->nextRequest:I
    add-int/lit8 v1, v1, 0x1
    sput v1, {klass}->nextRequest:I
    new-instance v0, Landroid/net/NetworkRequest;
    const/4 v2, -0x1
    invoke-direct {{v0, p0, v2, v1}}, Landroid/net/NetworkRequest;-><init>(Landroid/net/NetworkCapabilities;II)V
    return-object v0
.end method

.method private static ethernet()Landroid/net/NetworkInfo;
    .locals 5
    new-instance v0, Landroid/net/NetworkInfo;
    const/4 v1, 0x1
    const/4 v2, 0x0
    const-string v3, "WIFI"
    const-string v4, ""
    invoke-direct {{v0, v1, v2, v3, v4}}, Landroid/net/NetworkInfo;-><init>(IILjava/lang/String;Ljava/lang/String;)V
    sget-object v1, Landroid/net/NetworkInfo$DetailedState;->CONNECTED:Landroid/net/NetworkInfo$DetailedState;
    const/4 v2, 0x0
    invoke-virtual {{v0, v1, v2, v2}}, Landroid/net/NetworkInfo;->setDetailedState(Landroid/net/NetworkInfo$DetailedState;Ljava/lang/String;Ljava/lang/String;)V
    const/4 v1, 0x1
    invoke-virtual {{v0, v1}}, Landroid/net/NetworkInfo;->setIsAvailable(Z)V
    return-object v0
.end method
"""
for signature in methods:
    result = signature.split(")")[1]
    if signature in special:
        instructions = special[signature]
    elif result == "V":
        instructions = "return-void"
    elif result.startswith("["):
        instructions = f"const/4 v0, 0x0\n    new-array v0, v0, {result}\n    return-object v0"
    elif result.startswith("L"):
        instructions = "const/4 v0, 0x0\n    return-object v0"
    elif result in ("J", "D"):
        instructions = "const-wide/16 v0, 0x0\n    return-wide v0"
    else:
        instructions = "const/4 v0, 0x0\n    return v0"
    body += f"\n.method public {signature}\n    .locals 2\n    {instructions}\n.end method\n"
server.with_name("KoboHostConnectivity.smali").write_text(body)
anchor = "    invoke-virtual {v4, v8}, Lcom/android/server/am/ActivityManagerService;->systemReady(Ljava/lang/Runnable;)V"
call = f"    invoke-static {{}}, {klass}->publishIfMissing()V\n\n"
source = server.read_text()
# Re-running on an already patched tree replaces the earlier offline publisher.
earlier = server.with_name("KoboOfflineConnectivity.smali")
if earlier.exists():
    earlier.unlink()
    source = source.replace("    invoke-static {}, Lcom/android/server/KoboOfflineConnectivity;->publishIfMissing()V\n\n", "")
assert source.count(anchor) == 1 and klass not in source
server.write_text(source.replace(anchor, call + anchor))
