//go:build !windows

package main

import "os/exec"

func hideWindow(*exec.Cmd) {}

func isWindows() bool { return false }
