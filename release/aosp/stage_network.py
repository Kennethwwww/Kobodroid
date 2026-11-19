"""Generate the Kobo network facades against the checked-out Nougat AIDL."""
import argparse
from pathlib import Path
import re

SERVER = Path("frameworks/base/services/java/com/android/server")
NET = Path("frameworks/base/core/java/android/net/IConnectivityManager.aidl")
WIFI = Path("frameworks/base/wifi/java/android/net/wifi/IWifiManager.aidl")
ANCHOR = "        mActivityManagerService.systemReady(new Runnable() {"
CALL = """        if (SystemProperties.getBoolean("config.disable_network", false)) {
            KoboHostConnectivity.publishIfMissing();
            KoboHostWifi.publishIfMissing();
        }

"""
CONNECTED = """    private static final java.util.concurrent.atomic.AtomicInteger nextRequest =
            new java.util.concurrent.atomic.AtomicInteger();

    private static NetworkInfo connected() {
        NetworkInfo info = new NetworkInfo(1, 0, "WIFI", "");
        info.setDetailedState(NetworkInfo.DetailedState.CONNECTED, null, null);
        info.setIsAvailable(true);
        return info;
    }

    private static NetworkRequest request(NetworkCapabilities capabilities) {
        return new NetworkRequest(capabilities == null ? new NetworkCapabilities() : capabilities,
                -1, nextRequest.incrementAndGet());
    }
"""
CONNECTION_METHODS = {
    "getActiveNetworkInfo": "return connected();",
    "getActiveNetworkInfoForUid": "return connected();",
    "getNetworkInfoForUid": "return connected();",
    "getNetworkInfo": "return networkType == 1 ? connected() : null;",
    "getAllNetworkInfo": "return new NetworkInfo[] { connected() };",
    "getActiveNetwork": "return new Network(100);",
    "getActiveNetworkForUid": "return new Network(100);",
    "getAllNetworks": "return new Network[] { new Network(100) };",
    "getNetworkCapabilities": "return new NetworkCapabilities().addTransportType(1)"
                              ".addCapability(11).addCapability(12).addCapability(13)"
                              ".addCapability(14).addCapability(15).addCapability(16);",
    **{method: "return request(networkCapabilities);" for method in (
        "listenForNetwork", "requestNetwork", "pendingRequestForNetwork"
    )},
}
WIFI_METHODS = {
    "getConnectionInfo": """WifiInfo info = new WifiInfo();
        info.setSSID(android.net.wifi.WifiSsid.createFromAsciiEncoded("Kobo"));
        info.setSupplicantState(android.net.wifi.SupplicantState.COMPLETED);
        info.setRssi(-50);
        info.setLinkSpeed(65);
        info.setFrequency(2437);
        info.setNetworkId(0);
        return info;""",
    "getWifiEnabledState": "return 3;",
    "getWifiApEnabledState": "return 11;",
    "getDhcpInfo": "return new DhcpInfo();",
    "getCurrentNetwork": "return new Network(100);",
}


def default_value(result):
    if result == "void":
        return ""
    if result == "boolean":
        return "return false;"
    if result in {"byte", "short", "int", "long", "float", "double", "char"}:
        return "return 0;"
    if result.endswith("[]"):
        return f"return new {result[:-2]}[0];"
    if result.startswith("List<"):
        return "return new java.util.ArrayList<>();"
    return "return null;"


def generate(aidl, name, service, special, helpers=""):
    source = re.sub(r"/\*.*?\*/|//[^\n]*", "", aidl.read_text(), flags=re.S)
    interface = re.search(r"\binterface\s+(\w+)\s*\{(.*)\}", source, re.S)
    package = re.search(r"\bpackage\s+([\w.]+);", source)[1]
    parent, body = interface.groups()
    imports = re.findall(r"^import\s+[\w.]+;", source, re.M)
    if re.search(r"\bIBinder\b", body):
        imports.append("import android.os.IBinder;")
    imports += ["import android.os.ServiceManager;", "import java.util.List;",
                f"import {package}.{parent};"]
    methods = []
    found = set()
    for declaration in body.split(";"):
        declaration = " ".join(declaration.split())
        if not declaration:
            continue
        parts = re.fullmatch(r"(?:oneway )?([\w.<>\[\]]+) (\w+)\((.*)\)", declaration)
        if not parts:
            raise ValueError(f"Unsupported AIDL declaration: {declaration}")
        result, method, args = parts.groups()
        args = re.sub(r"\b(?:inout|in|out) ", "", args)
        code = special[method] if method in special else default_value(result)
        methods.append(f"    @Override\n    public {result} {method}({args}) {{\n        {code}\n    }}")
        found.add(method)
    if not set(special) <= found:
        raise ValueError(f"Missing expected methods in {aidl}")
    return (
        "package com.android.server;\n\n" + "\n".join(sorted(set(imports)))
        + f'\n\nfinal class {name} extends {parent}.Stub {{\n'
        + f'    static void publishIfMissing() {{\n        if (ServiceManager.checkService("{service}") == null) {{\n'
        + f'            ServiceManager.addService("{service}", new {name}());\n        }}\n    }}\n'
        + helpers + "\n" + "\n\n".join(methods) + "\n}\n"
    )


def stage(aosp):
    server = aosp / SERVER / "SystemServer.java"
    source = server.read_text()
    if source.count(ANCHOR) != 1:
        raise ValueError("Unexpected SystemServer startup")
    variants = (
        (NET, "KoboHostConnectivity", "connectivity", CONNECTION_METHODS, CONNECTED),
        (WIFI, "KoboHostWifi", "wifi", WIFI_METHODS, ""),
    )
    generated = [(name, generate(aosp / aidl, name, service, methods, helpers))
                 for aidl, name, service, methods, helpers in variants]
    for name, data in generated:
        target = server.with_name(name + ".java")
        if not target.exists() or target.read_text() != data:
            target.write_text(data)
    if CALL not in source:
        server.write_text(source.replace(ANCHOR, CALL + ANCHOR))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("aosp", type=Path)
    stage(parser.parse_args().aosp)
