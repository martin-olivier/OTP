use clap::{Parser, Subcommand, ValueEnum};
use std::{fs::{File, OpenOptions}, io::{Read, Write}, os::fd::AsRawFd};

/// OTP tools - manage your OTP devices
#[derive(Debug, Parser)]
#[clap(version, about, long_about = None)]
pub struct App {
    #[clap(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand, PartialEq, Eq)]
enum Command {
    /// Display devices status
    Status,

    /// Change the numbers of devices
    SetDevices {
        /// OTP device path
        devices: u8,
    },

    /// Change a device mode
    SetMode {
        /// OTP device path
        device: String,
        /// Device mode
        #[arg(value_enum)]
        mode: Mode,
    },

    /// Request a one time password from a device
    Request {
        /// OTP device path
        device: String,
    },

    /// Validate a one time password on a device
    Validate {
        /// OTP device path
        device: String,
        /// One time password
        otp: String,
    }
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum, Debug)]
enum Mode {
    List,
    Algo,
}

const PROC: &str = "/proc/otp";
const DEVICES_ARG: &str = "/sys/module/otp/parameters/devices";

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum, Debug)]
enum OpenMode {
    Read,
    Write,
}

fn open_file(path: &str, mode: OpenMode) -> File {
    match OpenOptions::new()
        .read(if mode == OpenMode::Read {true} else {false})
        .write(if mode == OpenMode::Write {true} else {false})
        .create(false)
        .open(path) {
            Ok(file) => file,
            Err(err) => {
                eprintln!("Failed to open '{}': {}", path, err);
                std::process::exit(1);
            }
    }
}

fn main() {
    // parse arguments
    let args = App::parse();

    // handle command
    match args.command {
        Command::Status => {
            let mut buffer = Vec::new();
            let mut proc = open_file(PROC, OpenMode::Read);

            if let Err(e) = proc.read_to_end(&mut buffer) {
                eprintln!("failed to read from device '{}': {}", PROC, e);
                std::process::exit(1);
            }

            let status = String::from_utf8(buffer).unwrap();

            print!("{}", status);
        },
        Command::SetDevices { devices } => {
            let mut dev_arg = open_file(DEVICES_ARG, OpenMode::Write);

            match dev_arg.write_all(devices.to_string().as_bytes()) {
                Ok(()) => println!("there is now {} otp devices", devices),
                Err(e) => {
                    eprintln!("devices argument as been rejected: '{}'", e);
                    std::process::exit(1);
                }
            }
        }
        Command::SetMode { device, mode } => {
            let dev = open_file(&device, OpenMode::Write);

            let mode_id = match mode {
                Mode::List => 0,
                Mode::Algo => 1,
            };

            unsafe {
                if libc::ioctl(dev.as_raw_fd(), mode_id) == -1 {
                    panic!("ioctl failed");
                }
            }

            println!("device '{}' has been set to mode '{:?}'", device, mode);
        },
        Command::Request { device } => {
            let mut buffer = Vec::new();
            let mut dev = open_file(&device, OpenMode::Read);

            if let Err(e) = dev.read_to_end(&mut buffer) {
                eprintln!("failed to read from device '{}': {}", device, e);
                std::process::exit(1);
            }

            let otp = String::from_utf8(buffer).unwrap();

            println!("device '{}' returned requested otp: {}", device, otp);
        },
        Command::Validate { device, otp } => {
            let mut dev = open_file(&device, OpenMode::Write);

            match dev.write_all(otp.as_bytes()) {
                Ok(()) => println!("otp has been approved by device '{}'", device),
                Err(_) => {
                    eprintln!("otp has been rejected by device '{}'", device);
                    std::process::exit(1);
                }
            }
        },
    }
}
