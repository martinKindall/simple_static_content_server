variable "region" {
  description = "AWS region"
  type        = string
}

variable "availability_zone" {
  description = "AWS availability zone"
  type        = string
}

variable "profile" {
  description = "AWS CLI profile name"
  type        = string
}

variable "key_name" {
  description = "Name of the EC2 key pair"
  type        = string
}
