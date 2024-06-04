#!/bin/bash
set -ex

# install pylspclient from repo using HTTPS
pip install git+https://github.com/metalware-inc/pylspclient.git
python3 integration_tests.py
