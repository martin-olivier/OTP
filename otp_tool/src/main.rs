use clap::{Parser, Subcommand, ValueEnum};
use std::{fs::OpenOptions, io::Write, os::fd::AsRawFd};

/// OTP tools
#[derive(Debug, Parser)]
#[clap(version, about, long_about = None)]
pub struct App {
    #[clap(long, value_parser)]
    device: String,

    #[clap(subcommand)]
    command: Command,
}

#[derive(Debug, Subcommand)]
enum Command {
    /// Change the device mode
    SetMode {
        /// Device mode, possible values: [list, algo]
        #[arg(value_enum)]
        #[clap(long, value_parser)]
        mode: Mode,
    },
    /// Validate a one time password
    Validate {
        /// One time password
        #[clap(long, value_parser)]
        otp: String,
    }
}

#[derive(Copy, Clone, PartialEq, Eq, PartialOrd, Ord, ValueEnum, Debug)]
pub enum Mode {
    List,
    Algo,
}

fn main() {
    let args = App::parse();

    let mut file = OpenOptions::new()
        .write(true)
        .create(false)
        .open(&args.device)
        .unwrap();

    match args.command {
        Command::SetMode { mode } => {
            let mode_id = match mode {
                Mode::List => 0,
                Mode::Algo => 1,
            };

            unsafe {
                if libc::ioctl(file.as_raw_fd(), mode_id) == -1 {
                    panic!("Ioctl failed");
                }
            }

            println!("device '{}' has been set to mode '{:?}'", args.device, mode);
        },
        Command::Validate { otp } => {
            match file.write_all(otp.as_bytes()) {
                Ok(()) => println!("otp has been approved by device '{}'", args.device),
                Err(_) => panic!("otp has been rejected by device '{}'", args.device),
            }
        },
    }
}
