"""Publish a Wi-Fi Binder when the real service is absent.

The connectivity stub reports one connected Wi-Fi network (apps such as WeRead only download books on Wi-Fi), so
WifiManager must answer too: WebView asks it for the link speed. The Kobo's own Wi-Fi is the connection; this stub
reports it enabled and connected, and every other call keeps an inert default (empty lists instead of null)."""
from pathlib import Path
import re
import sys

root, interface = map(Path, sys.argv[1:])
server = root / "com/android/server/SystemServer.smali"
stub = "Landroid/net/wifi/IWifiManager$Stub;"
klass = "Lcom/android/server/KoboHostWifi;"
methods = re.findall(r"^\.method public abstract (\S+)$", interface.read_text(), re.M)
assert "getConnectionInfo()Landroid/net/wifi/WifiInfo;" in methods
body = f""".class public final {klass}
.super {stub}

.method public constructor <init>()V
    .locals 0
    invoke-direct {{p0}}, {stub}-><init>()V
    return-void
.end method

.method public static publishIfMissing()V
    .locals 2
    const-string v0, "wifi"
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
info = """new-instance v0, Landroid/net/wifi/WifiInfo;
    invoke-direct {v0}, Landroid/net/wifi/WifiInfo;-><init>()V
    const-string v1, "Kobo"
    invoke-static {v1}, Landroid/net/wifi/WifiSsid;->createFromAsciiEncoded(Ljava/lang/String;)Landroid/net/wifi/WifiSsid;
    move-result-object v1
    invoke-virtual {v0, v1}, Landroid/net/wifi/WifiInfo;->setSSID(Landroid/net/wifi/WifiSsid;)V
    sget-object v1, Landroid/net/wifi/SupplicantState;->COMPLETED:Landroid/net/wifi/SupplicantState;
    invoke-virtual {v0, v1}, Landroid/net/wifi/WifiInfo;->setSupplicantState(Landroid/net/wifi/SupplicantState;)V
    const/16 v1, -0x32
    invoke-virtual {v0, v1}, Landroid/net/wifi/WifiInfo;->setRssi(I)V
    const/16 v1, 0x41
    invoke-virtual {v0, v1}, Landroid/net/wifi/WifiInfo;->setLinkSpeed(I)V
    const/16 v1, 0x985
    invoke-virtual {v0, v1}, Landroid/net/wifi/WifiInfo;->setFrequency(I)V
    const/4 v1, 0x0
    invoke-virtual {v0, v1}, Landroid/net/wifi/WifiInfo;->setNetworkId(I)V
    return-object v0"""
special = {
    "getConnectionInfo()Landroid/net/wifi/WifiInfo;": info,
    "getWifiEnabledState()I": "const/4 v0, 0x3\n    return v0",
    "getWifiApEnabledState()I": "const/16 v0, 0xb\n    return v0",
    "getDhcpInfo()Landroid/net/DhcpInfo;": "new-instance v0, Landroid/net/DhcpInfo;\n    invoke-direct {v0}, Landroid/net/DhcpInfo;-><init>()V\n    return-object v0",
    "getCurrentNetwork()Landroid/net/Network;": "new-instance v0, Landroid/net/Network;\n    const/16 v1, 0x64\n    invoke-direct {v0, v1}, Landroid/net/Network;-><init>(I)V\n    return-object v0",
}
assert set(special) <= set(methods)
for signature in methods:
    result = signature.split(")")[1]
    if signature in special:
        instructions = special[signature]
    elif result == "V":
        instructions = "return-void"
    elif result == "Ljava/util/List;":
        instructions = "new-instance v0, Ljava/util/ArrayList;\n    invoke-direct {v0}, Ljava/util/ArrayList;-><init>()V\n    return-object v0"
    elif result.startswith("["):
        instructions = f"const/4 v0, 0x0\n    new-array v0, v0, {result}\n    return-object v0"
    elif result.startswith("L"):
        instructions = "const/4 v0, 0x0\n    return-object v0"
    elif result in ("J", "D"):
        instructions = "const-wide/16 v0, 0x0\n    return-wide v0"
    else:
        instructions = "const/4 v0, 0x0\n    return v0"
    body += f"\n.method public {signature}\n    .locals 2\n    {instructions}\n.end method\n"
server.with_name("KoboHostWifi.smali").write_text(body)
anchor = "    invoke-static {}, Lcom/android/server/KoboHostConnectivity;->publishIfMissing()V\n"
source = server.read_text()
assert source.count(anchor) == 1 and klass not in source
server.write_text(source.replace(anchor, anchor + f"    invoke-static {{}}, {klass}->publishIfMissing()V\n"))
