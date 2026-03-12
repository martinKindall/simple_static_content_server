terraform {
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

provider "aws" {
  region  = "eu-central-1"
  profile = "macornejo"
}

data "aws_vpc" "default" {
  default = true
}

data "aws_ami" "al2023" {
  most_recent = true
  owners      = ["amazon"]

  filter {
    name   = "name"
    values = ["al2023-ami-*-x86_64"]
  }
}

data "aws_subnet" "default_az" {
  vpc_id            = data.aws_vpc.default.id
  availability_zone = "eu-central-1a"
  default_for_az    = true
}

resource "aws_security_group" "web_ssh" {
  name   = "web-ssh"
  vpc_id = data.aws_vpc.default.id

  ingress {
    from_port   = 22
    to_port     = 22
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }

  ingress {
    from_port   = 80
    to_port     = 80
    protocol    = "tcp"
    cidr_blocks = ["0.0.0.0/0"]
  }

  egress {
    from_port   = 0
    to_port     = 0
    protocol    = "-1"
    cidr_blocks = ["0.0.0.0/0"]
  }
}

resource "aws_instance" "web" {
  ami                         = data.aws_ami.al2023.id
  instance_type               = "t3.micro"
  availability_zone           = "eu-central-1a"
  associate_public_ip_address = true
  key_name                    = "frankfurt_v2"
  vpc_security_group_ids      = [aws_security_group.web_ssh.id]
  subnet_id                   = data.aws_subnet.default_az.id
}
