@echo off
setlocal enabledelayedexpansion

pip install pylspclient
python integration_tests.py
