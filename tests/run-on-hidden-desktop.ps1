# Runs the GUI tests on Qt's Windows platform plugin, on a desktop of their own
# that is never shown: real windows, the real style and the GPU, but nothing
# appears on (or takes input from) the desktop you are working on.
#
#   tests\run-on-hidden-desktop.ps1 <build-dir> [ctest arguments...]
#
# The build has to leave the platform to the environment:
#   cmake -S . -B build-win -DQFLEXDOCK_TEST_PLATFORM=
# With QFLEXDOCK_TEST_RHI=1 set, tst_quick renders its scenes with the GPU, as
# an application does, instead of the software renderer.
#
# ctest and the Qt libraries must be on PATH, as for any run of the tests. The
# tests and whatever they start end with this script, also if it is stopped.
param(
    [Parameter(Mandatory = $true, Position = 0)] [string] $BuildDir,
    [Parameter(ValueFromRemainingArguments = $true)] [string[]] $CTestArguments
)

$ErrorActionPreference = 'Stop'

$build = (Resolve-Path -LiteralPath $BuildDir).Path
$cache = Join-Path $build 'CMakeCache.txt'
if ((Test-Path -LiteralPath $cache) -and
    (Select-String -LiteralPath $cache -Pattern '^QFLEXDOCK_TEST_PLATFORM:[A-Z]+=.+' -Quiet)) {
    [Console]::Error.WriteLine("$build sets a platform for the tests. Configure it with " +
                               "-DQFLEXDOCK_TEST_PLATFORM= (empty) to run them on the Windows platform.")
    exit 2
}

if (-not ('QFlexDockTests.HiddenDesktop' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace QFlexDockTests
{
    public static class HiddenDesktop
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct STARTUPINFO
        {
            public int cb; public string lpReserved; public string lpDesktop; public string lpTitle;
            public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_BASIC_LIMIT_INFORMATION
        {
            public long PerProcessUserTimeLimit, PerJobUserTimeLimit;
            public uint LimitFlags; public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
            public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct IO_COUNTERS
        {
            public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
            public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
        {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
            public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateDesktop(string name, IntPtr device, IntPtr devmode, int flags,
                                           uint access, IntPtr security);
        [DllImport("user32.dll", SetLastError = true)]
        static extern bool CloseDesktop(IntPtr desktop);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern bool CreateProcess(string application, StringBuilder commandLine, IntPtr processSecurity,
                                         IntPtr threadSecurity, bool inheritHandles, uint flags,
                                         IntPtr environment, string directory, ref STARTUPINFO startup,
                                         out PROCESS_INFORMATION process);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr CreateJobObject(IntPtr security, string name);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool SetInformationJobObject(IntPtr job, int infoClass,
                                                   ref JOBOBJECT_EXTENDED_LIMIT_INFORMATION info, int length);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
        [DllImport("kernel32.dll")] static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll")] static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll")] static extern bool GetExitCodeProcess(IntPtr process, out uint code);
        [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

        // Runs `commandLine` on a new desktop and returns its exit code. The
        // process and everything it starts belong to a job that ends with the
        // caller, so nothing is left running where nobody can see it.
        public static int Run(string commandLine, string directory)
        {
            const uint GENERIC_ALL = 0x10000000;
            const uint CREATE_SUSPENDED = 0x00000004, CREATE_NO_WINDOW = 0x08000000;
            const uint JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x2000;
            const int JobObjectExtendedLimitInformation = 9;

            string name = "QFlexDockTests-" + Guid.NewGuid().ToString("N");
            IntPtr desktop = CreateDesktop(name, IntPtr.Zero, IntPtr.Zero, 0, GENERIC_ALL, IntPtr.Zero);
            if (desktop == IntPtr.Zero)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateDesktop");
            IntPtr job = CreateJobObject(IntPtr.Zero, null);
            try {
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (job == IntPtr.Zero
                    || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, ref limits,
                                                Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION))))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "job object");

                STARTUPINFO startup = new STARTUPINFO();
                startup.cb = Marshal.SizeOf(typeof(STARTUPINFO));
                startup.lpDesktop = "WinSta0\\" + name;
                PROCESS_INFORMATION process;
                if (!CreateProcess(null, new StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, false,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW, IntPtr.Zero, directory,
                                   ref startup, out process))
                    throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateProcess");
                try {
                    if (!AssignProcessToJobObject(job, process.hProcess))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "AssignProcessToJobObject");
                    ResumeThread(process.hThread);
                    // In slices, so that Ctrl+C gets a chance.
                    while (WaitForSingleObject(process.hProcess, 200) != 0) { }
                    uint code;
                    GetExitCodeProcess(process.hProcess, out code);
                    return unchecked((int)code);
                } finally {
                    CloseHandle(process.hThread);
                    CloseHandle(process.hProcess);
                }
            } finally {
                if (job != IntPtr.Zero)
                    CloseHandle(job); // ends whatever is still running
                CloseDesktop(desktop);
            }
        }
    }
}
'@
}

# The tests have no console there: what they print goes through a file.
$log = Join-Path ([IO.Path]::GetTempPath()) ("qflexdock-tests-" + [Guid]::NewGuid().ToString('N') + '.log')
$arguments = @('--test-dir', $build, '--output-on-failure') + @($CTestArguments | Where-Object { $_ })
# (Quoted where cmd would otherwise read something into them: "a|b" for -R.)
$quoted = $arguments | ForEach-Object { if ($_ -match '[\s"|&<>^()]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }
$command = 'cmd.exe /d /s /c "ctest ' + ($quoted -join ' ') + ' > "' + $log + '" 2>&1"'

$platform = $env:QT_QPA_PLATFORM
$env:QT_QPA_PLATFORM = 'windows'
try {
    $code = [QFlexDockTests.HiddenDesktop]::Run($command, $build)
} finally {
    $env:QT_QPA_PLATFORM = $platform
}
if (Test-Path -LiteralPath $log) {
    Get-Content -LiteralPath $log
    Remove-Item -LiteralPath $log
}
exit $code
