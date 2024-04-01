use clap::{Parser, Subcommand, ValueEnum};
use std::{fs::OpenOptions, io::{Read, Write}, os::fd::AsRawFd};

/// OTP tools - manage your OTP devices
#[derive(Debug, Parser)]
#[clap(version, about, long_about = None)]
pub struct App {
    /// OTP device path
    device: String,

    #[clap(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand, PartialEq, Eq)]
enum Command {
    /// Change the device mode
    SetMode {
        /// Device mode
        #[arg(value_enum)]
        mode: Mode,
    },

    /// Request a one time password
    Request,

    /// Validate a one time password
    Validate {
        /// One time password
        otp: String,
    }
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum, Debug)]
pub enum Mode {
    List,
    Algo,
}

fn main() {
    // parse arguments
    let args = App::parse();

    // open device
    let mut device = match OpenOptions::new()
        .read(if args.command == Command::Request { true } else { false })
        .write(if args.command == Command::Request { false } else { true })
        .create(false)
        .open(&args.device) {
            Ok(device) => device,
            Err(e) => {
                eprintln!("Failed to open device '{}': {}", args.device, e);
                std::process::exit(1);
            }
        };

    // handle command
    match args.command {
        Command::SetMode { mode } => {
            let mode_id = match mode {
                Mode::List => 0,
                Mode::Algo => 1,
            };

            unsafe {
                if libc::ioctl(device.as_raw_fd(), mode_id) == -1 {
                    panic!("Ioctl failed");
                }
            }

            println!("device '{}' has been set to mode '{:?}'", args.device, mode);
        },
        Command::Request => {
            let mut buffer = Vec::new();

            if let Err(e) = device.read_to_end(&mut buffer) {
                eprintln!("Failed to read from device '{}': {}", args.device, e);
                std::process::exit(1);
            }

            let otp = String::from_utf8(buffer).unwrap();

            println!("device '{}' returned requested otp: {}", args.device, otp);
        },
        Command::Validate { otp } => {
            match device.write_all(otp.as_bytes()) {
                Ok(()) => println!("otp has been approved by device '{}'", args.device),
                Err(_) => {
                    eprintln!("otp has been rejected by device '{}'", args.device);
                    std::process::exit(1);
                }
            }
        },
    }
}
