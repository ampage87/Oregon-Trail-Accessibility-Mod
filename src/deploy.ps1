# Oregon Trail Accessibility Mod - deploy progress window.
# Started by deploy.vbs.  Runs src\deploy.bat, shows each step in a read-only
# text box that screen readers announce, and saves Logs\build_latest.log.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$src  = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent $src
$logs = Join-Path $root 'Logs'
if (-not (Test-Path $logs)) { New-Item -ItemType Directory -Path $logs | Out-Null }
$logFile = Join-Path $logs 'build_latest.log'

# A text box that tells screen readers when its text changes.
Add-Type -ReferencedAssemblies System.Windows.Forms -TypeDefinition @'
using System.Windows.Forms;
public class SpeakingTextBox : TextBox {
    public void Announce() {
        AccessibilityNotifyClients(AccessibleEvents.NameChange, -1);
        AccessibilityNotifyClients(AccessibleEvents.ValueChange, -1);
    }
}
'@

$form = New-Object System.Windows.Forms.Form
$form.Text = 'Oregon Trail Accessibility Mod - Deploy'
$form.Size = New-Object System.Drawing.Size(560, 220)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.TopMost = $true

$status = New-Object SpeakingTextBox
$status.ReadOnly = $true
$status.Multiline = $true
$status.Location = New-Object System.Drawing.Point(12, 12)
$status.Size = New-Object System.Drawing.Size(520, 110)
$status.AccessibleName = 'Deploy status'
$status.Text = 'Starting deploy...'
$form.Controls.Add($status)

$bar = New-Object System.Windows.Forms.ProgressBar
$bar.Location = New-Object System.Drawing.Point(12, 132)
$bar.Size = New-Object System.Drawing.Size(520, 20)
$bar.Minimum = 0
$bar.Maximum = 6
$bar.AccessibleName = 'Deploy progress'
$form.Controls.Add($bar)

function Set-Status([string]$text) {
    $status.Text = $text
    $status.SelectionStart = 0
    $status.SelectionLength = 0
    $status.Announce()
    [System.Windows.Forms.Application]::DoEvents()
}

$form.Add_Shown({
    $form.Activate()
    $status.Focus()
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.Add("Oregon Trail Accessibility Mod deploy - $(Get-Date)")
    $failed = $false
    $warning = ''
    $lastError = ''

    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $env:ComSpec
    $psi.Arguments = '/c ""' + (Join-Path $src 'deploy.bat') + '" 2>&1"'
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.CreateNoWindow = $true
    $psi.WorkingDirectory = $root
    $p = [System.Diagnostics.Process]::Start($psi)
    while (-not $p.StandardOutput.EndOfStream) {
        $line = $p.StandardOutput.ReadLine()
        $lines.Add($line)
        if ($line -match '^STEP (\d+)/(\d+): (.*)$') {
            $bar.Value = [Math]::Min([int]$Matches[1] - 1, $bar.Maximum)
            Set-Status ("Step {0} of {1}: {2}" -f $Matches[1], $Matches[2], $Matches[3])
        } elseif ($line -match '^ERROR: (.*)$') {
            $lastError = $Matches[1]
            $failed = $true
            Set-Status ('Error: ' + $lastError)
        } elseif ($line -match '^WARN: (.*)$') {
            $warning = $Matches[1]
        } elseif ($line -match '\berror C\d+') {
            if (-not $lastError) { $lastError = $line.Trim() }
        }
        [System.Windows.Forms.Application]::DoEvents()
    }
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { $failed = $true }
    $lines.Add("Exit code: $($p.ExitCode)")
    try { [System.IO.File]::WriteAllLines($logFile, $lines) } catch {}

    if (-not $failed) {
        $bar.Value = $bar.Maximum
        $msg = "The mod was built and installed.`r`n`r`nStart the game with OTAccess.exe in the mod folder."
        if ($warning) { $msg += "`r`n`r`nNote: $warning" }
        Set-Status 'Deploy finished.'
        [System.Windows.Forms.MessageBox]::Show($form, $msg, 'Deploy finished', 'OK', 'Information') | Out-Null
        $form.Close()
        return
    }

    if (-not $lastError) { $lastError = 'The build failed.' }
    Set-Status ('Deploy failed: ' + $lastError)
    $dlg = New-Object System.Windows.Forms.Form
    $dlg.Text = 'Deploy failed'
    $dlg.Size = New-Object System.Drawing.Size(520, 200)
    $dlg.StartPosition = 'CenterParent'
    $dlg.FormBorderStyle = 'FixedDialog'
    $dlg.MaximizeBox = $false
    $dlg.MinimizeBox = $false
    $txt = New-Object System.Windows.Forms.TextBox
    $txt.ReadOnly = $true
    $txt.Multiline = $true
    $txt.Location = New-Object System.Drawing.Point(12, 12)
    $txt.Size = New-Object System.Drawing.Size(480, 90)
    $txt.AccessibleName = 'Error'
    $txt.Text = "Deploy failed: $lastError`r`n`r`nThe full log is in Logs\build_latest.log."
    $dlg.Controls.Add($txt)
    $view = New-Object System.Windows.Forms.Button
    $view.Text = '&View Build Log'
    $view.Location = New-Object System.Drawing.Point(12, 115)
    $view.Size = New-Object System.Drawing.Size(140, 30)
    $view.Add_Click({ Start-Process notepad.exe -ArgumentList ('"' + $logFile + '"') })
    $dlg.Controls.Add($view)
    $close = New-Object System.Windows.Forms.Button
    $close.Text = '&Close'
    $close.Location = New-Object System.Drawing.Point(352, 115)
    $close.Size = New-Object System.Drawing.Size(140, 30)
    $close.DialogResult = 'Cancel'
    $dlg.Controls.Add($close)
    $dlg.CancelButton = $close
    $dlg.AcceptButton = $view
    $dlg.Add_Shown({ $txt.Focus() })
    $dlg.ShowDialog($form) | Out-Null
    $form.Close()
})

[System.Windows.Forms.Application]::Run($form)
