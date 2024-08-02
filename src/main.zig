const std = @import("std");
const c = @cImport({
    @cInclude("signal.h");
    @cInclude("stdio.h");
    @cInclude("X11/Xlib.h");
});

const blocks = [_]Block{
    .{ .icon = "", .command = "free -h | awk '/^Mem/ { print $3\"/\"$2 }' | sed s/i//g", .interval = 30, .signal = 0 },
    .{ .icon = "", .command = "~/.local/bin/battery", .interval = 15, .signal = 0 },
    .{ .icon = "", .command = "date '+%FT%H:%M v %V'", .interval = 5, .signal = 0 },
};

const delim = " | ";

const Block = struct {
    icon: []const u8,
    command: [*c]const u8,
    interval: c_uint,
    signal: c_int,
};

var writestatus: *const fn () anyerror!void = &setroot;

var dpy: ?*c.Display = null;
var screen: c_int = 0;
var root: c.Window = 0;
var status_continue = true;

const max_output_len = 50;
const Part = std.BoundedArray(u8, max_output_len);
var parts = [_]Part{.{}} ** blocks.len;

const max_status_len = (max_output_len + delim.len) * blocks.len - delim.len;
const Status = std.BoundedArray(u8, max_status_len + 1);
var status = [_]Status{.{}} ** 2;

fn dummysighandler(signum: c_int) callconv(.C) void {
    _ = signum;
}

fn sighandler(signum: c_int) callconv(.C) void {
    getsigcmds(signum - c.__libc_current_sigrtmax()) catch unreachable;
    writestatus() catch unreachable;
}

fn getcmds(time: c_uint) !void {
    for (&blocks, &parts) |*block, *part|
        if (time == 0 or block.interval != 0 and time % block.interval == 0)
            try getcmd(block, part);
}

fn getsigcmds(signal: c_int) !void {
    for (&blocks, &parts) |*block, *bar|
        if (block.signal == signal)
            try getcmd(block, bar);
}

fn setupsignals() void {
    var i = c.__libc_current_sigrtmin();
    const sigrtmax = c.__libc_current_sigrtmax();
    while (i <= sigrtmax) : (i += 1)
        _ = c.signal(i, &dummysighandler);

    for (blocks) |block| {
        if (block.signal > 0)
            _ = c.signal(c.__libc_current_sigrtmin() + block.signal, &sighandler);
    }
}

fn getstatus(str: *Status, last: *Status) bool {
    std.mem.swap(Status, str, last);
    str.len = 0;
    for (parts, 0..) |part, i| {
        if (i != 0) str.appendSliceAssumeCapacity(delim);
        str.appendSliceAssumeCapacity(part.slice());
    }
    str.appendAssumeCapacity('\x00');
    return !std.mem.eql(u8, str.slice(), last.slice());
}

fn statusloop() !void {
    setupsignals();
    var i: c_uint = 0;
    while (status_continue) : (i += 1) {
        try getcmds(i);
        try writestatus();
        std.time.sleep(std.time.ns_per_s);
    }
}

fn termhandler(signum: c_int) callconv(.C) void {
    _ = signum;
    status_continue = false;
}

fn pstdout() !void {
    if (!getstatus(&status[0], &status[1])) return;

    try std.io.getStdOut().writer().print("{s}\n", .{status[0].slice()});
}

fn setroot() !void {
    if (!getstatus(&status[0], &status[1])) return;

    _ = c.XStoreName(dpy, root, @ptrCast(&status[0]));
    _ = c.XFlush(dpy);
}

fn setupX() !void {
    dpy = c.XOpenDisplay(null) orelse return error.FailedToOpenDisplay;
    screen = c.DefaultScreen(dpy);
    root = c.RootWindow(dpy, screen);
}

fn getcmd(block: *const Block, output: *Part) !void {
    output.len = 0;
    try output.appendSlice(block.icon);

    const raw_cmd = c.popen(block.command, "r") orelse return;
    const cmd = std.fs.File{ .handle = c.fileno(raw_cmd) };
    defer _ = c.pclose(raw_cmd);
    try cmd.reader().readIntoBoundedBytes(max_output_len, output);

    if (std.mem.endsWith(u8, output.slice(), "\n")) _ = output.pop();
}

pub fn main() !void {
    var gpa = std.heap.GeneralPurposeAllocator(.{}){};
    defer std.debug.assert(gpa.deinit() == .ok);
    const allocator = gpa.allocator();

    var x = true;
    var args = try std.process.argsWithAllocator(allocator);
    defer args.deinit();
    while (args.next()) |arg| {
        if (std.mem.eql(u8, arg, "-p")) {
            x = false;
            writestatus = &pstdout;
            break;
        }
    }

    if (x) try setupX();

    _ = c.signal(c.SIGTERM, &termhandler);
    _ = c.signal(c.SIGINT, &termhandler);

    try statusloop();

    if (x) _ = c.XCloseDisplay(dpy);
}
