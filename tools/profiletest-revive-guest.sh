#!/bin/sh
# Test-only; injected into a disposable image, never shipped to a customer.
exec python3 /usr/lib/auros-profiletest/profiletest-revive.py guest
