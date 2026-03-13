## Simple Static Content Server

Serve your files like .html, .js, .css, .jpg and more either in you localhost or on the internet with this C implementation.

## Running locally

- Requirements:
  - Linux distro, because this implementation uses epoll()
  - gcc, make
  - sanitizers: libasan libubsan

Download your favourite wallpaper, name it __wallpaper.jpg__ and paste it into ./src/public/.

Then, execute these commands:

```bash
cd src
make
./server
```

Go to http://127.0.0.1:3490/index.html and you should see your website with the downloaded wallpaper.

## Running on AWS EC2

- Requirements
  - AWS CLI V2
  - Terraform
  - Ansible Core

Instructions:

- This project runs in eu-central-1.
- Make sure you are logged in with your aws cli, either with your sso account or with a valid aws profile.
- Generate a .pem file in eu-central-1 for EC2 instances.
- Modify __infra/main.tf__ and replace the __profile__ name with your profile, and the pem key name for your pem key.

Run terraform:

```bash
cd infra
terraform init
terraform plan
terraform apply
```

Once the changes are applied, you will see your instance being created on AWS Console. Wait a couple of minutes until the instance is ready. Note down its public ip as we will need it in the next step.

Ansible instructions:
- Modify __ansible.cfg__: private key file must point to your .pem in your local machine. Mine is called frankfurt, but you must use your own.
- Modify __inventory.ini__: under __web__, put the public ip of your EC2 instance.

Run:

```bash
cd playbook
ansible-playbook deploy.yml
```

Now go to your public ip, using http (and not https!).

http://<insert-public-ip>/index.html

and you should see the website, congratz!

