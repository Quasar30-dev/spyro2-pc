"""Start the installed Blender MCP addon in a private background session."""
import sys
sys.path.insert(0, r'C:\Users\Shadow\Documents\blender_mcp\addon')
from blender_mcp_addon import cli
cli.cli_execute(['--host', '127.0.0.1', '--port', '9876'])
