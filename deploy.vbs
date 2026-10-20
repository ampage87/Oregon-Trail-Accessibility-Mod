' Oregon Trail Accessibility Mod - deploy
' Builds the mod and puts its files where they belong. Double-click to run.
Option Explicit
Dim sh, fso, root, cmd
Set sh = CreateObject("WScript.Shell")
Set fso = CreateObject("Scripting.FileSystemObject")
root = fso.GetParentFolderName(WScript.ScriptFullName)
cmd = "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & root & "\src\deploy.ps1"""
sh.Run cmd, 0, False
