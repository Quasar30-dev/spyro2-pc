# Dump the UI Automation tree of every top-level window on the current desktop (TEST FIXTURE helper).
# ds_harness.py starts this on DuckStation's hidden desktop (UIA only sees windows of its own desktop),
# so Qt dialogs, which have no native child controls, can be read. Output: UTF-8 text file.
param([string]$Out)
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes
$lines = New-Object System.Collections.Generic.List[string]
function Walk($el, $depth) {
    if ($depth -gt 8) { return }
    $c = $el.Current
    $lines.Add(("  " * $depth) + "[" + $c.ControlType.ProgrammaticName + "] name='" + $c.Name + "' class='" + $c.ClassName + "' pid=" + $c.ProcessId)
    try {
        $vp = $el.GetCurrentPattern([System.Windows.Automation.ValuePattern]::Pattern)
        if ($vp) { $lines.Add(("  " * $depth) + "  value='" + $vp.Current.Value + "'") }
    } catch {}
    $kids = $el.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($k in $kids) { Walk $k ($depth + 1) }
}
try {
    $root = [System.Windows.Automation.AutomationElement]::RootElement
    $tops = $root.FindAll([System.Windows.Automation.TreeScope]::Children, [System.Windows.Automation.Condition]::TrueCondition)
    foreach ($t in $tops) { Walk $t 0 }
} catch { $lines.Add("ERROR " + $_) }
[System.IO.File]::WriteAllLines($Out, $lines, (New-Object System.Text.UTF8Encoding($false)))
