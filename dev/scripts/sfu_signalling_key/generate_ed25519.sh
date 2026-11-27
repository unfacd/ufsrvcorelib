#!/bin/sh
set -eu
umask 077

if [ "$#" -gt 1 ] || { [ "$#" -eq 1 ] && [ -z "$1" ]; }; then
    printf 'Usage: %s [NEW_OUTPUT_DIRECTORY]\n' "$0" >&2
    exit 2
fi

output=${1:-.}
case "$output" in
    /*|.) ;;
    *) output=./$output ;;
esac

created_directory=false
wrote_private=false
wrote_public=false
complete=false
staging=
cleanup() {
    if [ "$complete" = false ]; then
        if [ "$wrote_private" = true ]; then rm -f -- "$output/sfu-sginalling-ed25519.pem"; fi
        if [ "$wrote_public" = true ]; then rm -f -- "$output/sfu-sginalling-ed25519.pub"; fi
    fi
    if [ -n "$staging" ]; then
        rm -f -- "$staging/sfu-sginalling-ed25519.pem" "$staging/sfu-sginalling-ed25519.pub" "$staging/public.der"
        rmdir -- "$staging"
    fi
    if [ "$complete" = false ] && [ "$created_directory" = true ]; then
        rmdir -- "$output"
    fi
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

if [ "$#" -eq 1 ]; then
    mkdir -- "$output"
    created_directory=true
fi
staging=$(mktemp -d -- "$output/.sfu-signalling-key.XXXXXX")

openssl genpkey -algorithm ED25519 -out "$staging/sfu-sginalling-ed25519.pem"
openssl pkey -in "$staging/sfu-sginalling-ed25519.pem" -pubout -outform DER -out "$staging/public.der"

# An Ed25519 SubjectPublicKeyInfo is a 12-byte DER prefix and a 32-byte key.
prefix=$(od -An -tx1 -N12 "$staging/public.der" | tr -d ' \n')
size=$(wc -c < "$staging/public.der")
if [ "$prefix" != 302a300506032b6570032100 ] || [ "$size" -ne 44 ]; then
    printf 'Unexpected Ed25519 public key encoding\n' >&2
    exit 1
fi
public_key=$(tail -c 32 "$staging/public.der" | openssl base64 -A)
printf '%s\n' "$public_key" | tr '/+' '_-' | tr -d '=' > "$staging/sfu-sginalling-ed25519.pub"

# Publish each file without replacing existing keys, including concurrent writes.
ln -T -- "$staging/sfu-sginalling-ed25519.pem" "$output/sfu-sginalling-ed25519.pem"
wrote_private=true
ln -T -- "$staging/sfu-sginalling-ed25519.pub" "$output/sfu-sginalling-ed25519.pub"
wrote_public=true

complete=true
printf 'Chat signing key: %s/sfu-sginalling-ed25519.pem\nSFU verification key: %s/sfu-sginalling-ed25519.pub\n' "$output" "$output"
