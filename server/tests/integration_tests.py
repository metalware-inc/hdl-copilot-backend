import pylspclient
import unittest
import subprocess
import os
import time
from collections import defaultdict

from os import environ


DEFAULT_CAPABILITIES = {
    'textDocument': {
      'completion': {
        'completionItem': {
          'commitCharactersSupport': True,
          'documentationFormat': ['markdown', 'plaintext'],
          'snippetSupport': True
          }
        }
      }
    }

def to_uri(path: str, tok: str = "file") -> str:
  if os.name == 'nt':
    if path.startswith(tok + ":///"): return path
    return f"{tok}:///{path}"
  else:
    if path.startswith(tok + "://"): return path
    return f"{tok}://{path}"

def from_uri(path: str) -> str:
  res = path.replace("uri://", "").replace("uri:", "").replace("uri:///", "")
  res = res.replace("file:///", "").replace("file://", "")
  return res

def server_process() -> subprocess.Popen:
  if os.name == 'nt':
    cmd = ["../build/src/Release/hdl_copilot_server.exe"] # local testing only
    # cmd = ["../out/build/x64-Release/src/hdl_copilot_server.exe"]
  else:
    cmd = ["../build/src/hdl_copilot_server"]
  return subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.PIPE)

class TestLspClient(unittest.TestCase):
  def setUp(self):
    self._proc = server_process()
    self._lsp_endpoint = pylspclient.LspEndpoint(pylspclient.JsonRpcEndpoint(self._proc.stdin, self._proc.stdout),
                                                 notify_callbacks=dict({
                                                   "backend/licenseMissing": self._handle_license_missing,
                                                   "backend/licenseValid":   self._handle_license_valid,
                                                   "backend/licenseInvalid": self._handle_license_invalid,
                                                   "textDocument/publishDiagnostics": self._handle_publish_diagnostics,
                                                   "backend/cacheLicense": self._handle_cache_license,
                                                   "backend/macrosChanged": self._handle_macros_changed,
                                                   "backend/exclusionsChanged": self._handle_exclusions_changed
                                                   }))
    self._client = pylspclient.LspClient(self._lsp_endpoint)
    self._packets_received = defaultdict(list)
    self._provide_invalid_license = False
    # Git checkout the projects/ path since it may have been modified by the previous tests.
    os.system("git checkout projects")

  def tearDown(self):
    self._proc.kill()
    try:
      self._client.shutdown()
      self._client.exit()
    except Exception as e:
      print("tearDown: ", e)
    # Git checkout the projects/ path since it may have been modified by the previous tests.
    os.system("git checkout projects")

  def _handle_license_missing(self, pkt):
    self._recv_missing_license = True
    self._packets_received['backend/licenseMissing'].append(pkt)

  def _send_valid_license(self):
    self._lsp_endpoint.send_message("setLicenseKey", {'licenseKey':environ.get("HDL_COPILOT_LICENSE_KEY")})

  def _send_invalid_license(self):
    self._lsp_endpoint.send_message("setLicenseKey", {'licenseKey':"yolo"})

  def _send_project_path(self, path):
    self._lsp_endpoint.send_message("setProjectPath", {'path':path})

  def _send_exclude_resource(self, p):
    self._lsp_endpoint.send_message("excludeResource", {'path':p})

  def _send_include_resource(self, p):
    self._lsp_endpoint.send_message("includeResource", {'path':p})

  def _send_recompile_request(self):
    self._lsp_endpoint.send_message("recompile", {})

  def _send_set_macros(self, macros):
    self._lsp_endpoint.send_message("setMacros", {'macros':macros})

  def _send_did_change(self, uri, content):
    self._lsp_endpoint.send_message("textDocument/didChange", {
      "textDocument": {
        "uri": uri,
        "version": 1
      },
      "contentChanges": [{
        "text": content
      }]
    })

  def _send_did_close(self, uri, content):
    self._lsp_endpoint.send_message("textDocument/didClose", {
      "textDocument": {
        "uri": uri,
        "version": 1
      }
    })

  def _send_did_open(self, uri, content):
    self._lsp_endpoint.send_message("textDocument/didOpen", {
      "textDocument": {
        "uri": uri,
        "languageId": "systemverilog",
        "version": 1,
        "text": content
      }
    })

  def _handle_license_valid(self, pkt):
    self._packets_received['backend/licenseValid'].append(pkt)

  def _handle_license_invalid(self, pkt):
    self._packets_received['backend/licenseInvalid'].append(pkt)

  def _handle_cache_license(self, pkt):
    self._packets_received['backend/cacheLicense'].append(pkt)

  def _handle_macros_changed(self, pkt):
    self._packets_received['backend/macrosChanged'].append(pkt)

  def _handle_exclusions_changed(self, pkt):
    self._packets_received['backend/exclusionsChanged'].append(pkt)

  def _handle_publish_diagnostics(self, pkt):
    self._packets_received['textDocument/publishDiagnostics'].append(pkt)

  def _get_license_path(self):
    if os.name == 'posix' and os.uname().sysname == 'Darwin': return os.path.join("/tmp", "metalware-license.txt")
    elif os.name == 'nt': return os.path.join(os.environ['APPDATA'], "metalware-license.txt")
    else: return ".metalware-license.txt"

  def _rm_license(self):
    p = self._get_license_path()
    if os.path.exists(p): os.remove(p)

  def _create_license(self):
    p = self._get_license_path()
    with open(p, "w") as f: f.write(environ.get("HDL_COPILOT_LICENSE_KEY"))

  def _init(self, root_path):
    process_id = None
    root_path = None
    root_uri = os.path.abspath("projects/exclusions")
    print(root_uri)

    initialization_options = None
    capabilities = DEFAULT_CAPABILITIES
    trace = "off"
    workspace_folders = None
    initialize_response = self._client.initialize(process_id,
                                                  root_path,
                                                  root_uri,
                                                  initialization_options,
                                                  capabilities,
                                                  trace, workspace_folders)
    return initialize_response

  def _initialize_project(self, path):
    self._create_license()
    initialize_response = self._init(path)
    self._client.initialized()
    self.assertEqual(initialize_response['serverInfo']['name'], 'HDL Copilot Server')
    abs_path = os.path.abspath(path)
    self._send_project_path(abs_path)

    time.sleep(0.5)
    return os.path.abspath(path)

    self._rm_license()

    initialize_response = self._init("projects/exclusions")
    self._client.initialized()
    self.assertEqual(initialize_response['serverInfo']['name'], 'HDL Copilot Server')

    print("LSP Initialized")

    time.sleep(0.3)
    self._send_valid_license()
    time.sleep(0.3)
    self.assertTrue('backend/licenseValid' in self._packets_received)

  def test_initialize_cached_license(self):
    self._create_license()

    initialize_response = self._init("projects/exclusions")
    self._client.initialized()
    self.assertEqual(initialize_response['serverInfo']['name'], 'HDL Copilot Server')

    print("LSP Initialized")

  def test_set_project_path_missing_license(self):
    self._rm_license()

    initialize_response = self._init("projects/redefinitions")
    self._client.initialized()
    self.assertEqual(initialize_response['serverInfo']['name'], 'HDL Copilot Server')

    print("LSP Initialized")
    time.sleep(0.2)

    abspath = os.path.abspath("projects/redefinitions")

    self._send_project_path(abspath)
    # Make sure diagnostics were not received (yet)
    self.assertFalse('textDocument/publishDiagnostics' in self._packets_received)
    self._send_valid_license()
    time.sleep(0.2)
    self.assertTrue('backend/licenseValid' in self._packets_received)
    time.sleep(0.5)

    # Make sure diagnostics were received
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    # Make sure the license was "cached" (shared with the frontend). This is supposed
    # to happen at some point once a valid license is provided.
    self.assertTrue('backend/cacheLicense' in self._packets_received)

  def test_define_macro(self):
    self._create_license()
    abspath = os.path.abspath("projects/missing_macro")

    # Remove .metalware-project file
    dotfile_path = os.path.join(abspath, ".hdl-project")
    if os.path.exists(dotfile_path): os.remove(dotfile_path)
    # Recreate it
    with open(dotfile_path, "w") as f: f.write("{}")

    initialize_response = self._init("projects/missing_macro")
    self._client.initialized()

    self.assertEqual(initialize_response['serverInfo']['name'], 'HDL Copilot Server')

    print("LSP Initialized")
    self._send_project_path(abspath)

    # Make sure diagnostics were received despite not sending a license
    time.sleep(0.5)
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    # Make sure non empty diagnostics were received
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)
    for diag in diags:
      self.assertTrue(len(diag['diagnostics']) >= 1)

    self._packets_received['textDocument/publishDiagnostics'] = []

    # Define macro
    self._send_set_macros([{"name":"FOO", "value":"1"}])

    # Make sure macrosChanged was received
    time.sleep(0.5)
    self.assertTrue('backend/macrosChanged' in self._packets_received)

    # Send recompile request
    self._send_recompile_request()

    time.sleep(0.5)
    # Make sure an empty diagnostics packet was received
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)

    # Expect all diagnostics to be empty
    for diag in diags:
      self.assertTrue(len(diag['diagnostics']) == 0)

    # Undefine macro
    self._packets_received['textDocument/publishDiagnostics'] = []
    self._packets_received['backend/macrosChanged'] = []
    self._send_set_macros([])

    # Make sure macrosChanged was received
    time.sleep(0.5)
    self.assertTrue('backend/macrosChanged' in self._packets_received)

    # Send recompile request
    self._send_recompile_request()

    time.sleep(0.5)
    # Make sure an non-empty diagnostics packet was received
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)

    # Expect all diagnostics to be non-empty
    for diag in diags:
      print(diag)
      self.assertTrue(len(diag['diagnostics']) >= 1)

  def test_did_change(self):
    self._create_license()
    initialize_response = self._init("projects/missing_macro")
    self._client.initialized()
    self.assertEqual(initialize_response['serverInfo']['name'], 'HDL Copilot Server')

    abspath = os.path.abspath("projects/missing_macro")
    self._send_project_path(abspath)
    self._send_set_macros([])

    # Make sure diagnostics were received despite not sending a license
    time.sleep(0.5)
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)
    for diag in diags:
      self.assertTrue(len(diag['diagnostics']) >= 1)

    self._packets_received['textDocument/publishDiagnostics'] = []

    self._send_did_change(to_uri(abspath + "/main.sv", "file"), "module foo; endmodule")

    time.sleep(0.5)

    # Make sure diagnostics were received
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    # Make sure the diagnostics were empty to clear previous since updated doc is valid.
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)
    for diag in diags:
      self.assertTrue(len(diag['diagnostics']) == 0)

  def test_delete_bad_file(self):
    # This test ensure that when I delete a file that has a syntax error, the error is cleared.
    # When a file is deleted, we can assume a didClose event is sent to the server. Of course,
    # this is not handled if the file is deleted outside of the editor.
    abspath = self._initialize_project("projects/one_bad_file")

    time.sleep(0.2)
    print(self._packets_received)

    # Print diagnostics
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)

    # Rename the file to bad.sv.disabled
    os.rename(abspath + "/bad.sv", abspath + "/bad.sv.disabled")

    self._packets_received['textDocument/publishDiagnostics'] = []
    # Send didClose
    self._send_did_close(to_uri(abspath + "/bad.sv", "file"), "")
    time.sleep(0.25)

    # Make sure diagnostics were received
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    # Make sure the diagnostics were empty to clear previous since updated doc is valid.
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)
    # All diagnostics should clear now that the bed file has been removed.
    for diag in diags: self.assertTrue(len(diag['diagnostics']) == 0)

    # Move the file back
    os.rename(abspath + "/bad.sv.disabled", abspath + "/bad.sv")

  def test_add_missing_file(self):
    # This test ensure that when I add a file that contains the missing module causing
    # errors, those errors are cleared. The file is added via a didOpen event.
    # Rm file if it exists
    if os.path.exists("projects/missing_one_file/definitions.svh"):
       os.remove("projects/missing_one_file/definitions.svh")

    abs_path = self._initialize_project("projects/missing_one_file")
    time.sleep(0.3)

    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)

    # Create the missing file
    # test.sv
    with open(abs_path + "/definitions.svh", "w") as f: f.write("`define FOO 3")

    self._packets_received['textDocument/publishDiagnostics'] = []

    # Send didOpen
    self._send_did_open(to_uri(abs_path + "/definitions.svh", "file"), "`define FOO 3")
    time.sleep(0.2)
    # Make sure diagnostics were received
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)
    # Make sure the diagnostics were empty to clear previous since updated doc is valid.
    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)
    # All diagnostics should clear now that the bed file has been removed.
    for diag in diags:
      print(diags)
      self.assertTrue(len(diag['diagnostics']) == 0)

  def test_exclude(self):
    abspath = self._initialize_project("projects/redefinitions")

    # Make sure diagnostics were received despite not sending a license
    self.assertTrue('textDocument/publishDiagnostics' in self._packets_received)

    self._send_include_resource(abspath + "/foo_tb.sv")
    self._send_include_resource(abspath + "/foo2.sv")

    time.sleep(0.3)
    self.assertTrue('backend/exclusionsChanged' in self._packets_received)
    del self._packets_received['backend/exclusionsChanged']

    self._send_recompile_request()

    time.sleep(0.5)

    diags = self._packets_received['textDocument/publishDiagnostics']

    # Expect non-empty diagnostics for foo_tb.sv and foo2.sv
    expected_non_empty_basenames = ['foo_tb.sv', 'foo2.sv']

    # Make sure diagnostics were received for foo_tb.sv and foo2.sv
    for expected in expected_non_empty_basenames:
      self.assertTrue(any([os.path.basename(diag['uri']) == expected for diag in diags]))

    # Make sure diagnostics were non-empty for foo_tb.sv and foo2.sv
    for diag in diags:
      path = from_uri(diag['uri'])

      if os.path.basename(path) in ['foo2.sv', 'foo_tb.sv']:
        self.assertTrue(len(diag['diagnostics']) >= 1)

    self._packets_received['textDocument/publishDiagnostics'] = []
    # Exclude foo_tb.sv
    self._send_exclude_resource(abspath + "/foo_tb.sv")

    # Make sure exclusionsChanged was received
    time.sleep(0.3)
    self.assertTrue('backend/exclusionsChanged' in self._packets_received)

    self._send_recompile_request()

    time.sleep(0.5)

    diags = self._packets_received['textDocument/publishDiagnostics']
    self.assertTrue(len(diags) >= 1)

    # Expect all foo_tb.sv diagnostics to be empty now that it's excluded
    expected_empty_basenames = ['foo_tb.sv']

    # Make sure diagnostics were received for foo_tb.sv
    for expected in expected_empty_basenames:
      self.assertTrue(any([os.path.basename(diag['uri']) == expected for diag in diags]))

    # Make sure diagnostics were empty for foo_tb.sv
    for diag in diags:
      path = from_uri(diag['uri'])
      if os.path.basename(path) in expected_empty_basenames:
        self.assertTrue(len(diag['diagnostics']) == 0)

if __name__ == '__main__':
  # Print all environment variables
  print("Environment variables:")
  for k, v in environ.items():
    print(f"{k}={v}")
  print("Using key: ", environ.get("HDL_COPILOT_LICENSE_KEY"))
  unittest.main()
