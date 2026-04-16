./bootstrap.sh && ./task && echo "OK" || {
    echo "Oups! Something FAILED..."
    exit 1
}