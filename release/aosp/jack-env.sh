# Nougat's Jack server and client communicate over the container's loopback.
# Keep its compatible TLS policy local to these build processes.
jack_security=$(mktemp)
sed -n '/^jdk.tls.disabledAlgorithms=/,+2p' /etc/java-8-openjdk/security/java.security |
    sed 's/TLSv1, TLSv1.1, //g' > "$jack_security"
test -s "$jack_security"
export USER="${USER:-$(id -un)}"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:+$JAVA_TOOL_OPTIONS }-Djava.security.properties=$jack_security"
export JACK_EXTRA_CURL_OPTIONS="${JACK_EXTRA_CURL_OPTIONS:+$JACK_EXTRA_CURL_OPTIONS }--tlsv1.0"
