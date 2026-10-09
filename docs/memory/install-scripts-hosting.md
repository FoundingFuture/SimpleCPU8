# Install scripts hosting

foundingfuture.com serves packaging/install.sh and packaging/install.ps1
at https://foundingfuture.com/downloads/. The scripts, the README and
tests/ide/install_url_test.cpp name that address.

Copy both scripts to `triton:/var/www/foundingfuture.com/deploy/downloads/`
whenever either one changes:

```bash
scp packaging/install.sh packaging/install.ps1 triton:/var/www/foundingfuture.com/deploy/downloads/
```

The web team deploys that folder to the live site. Do not run the site's
`./d downloads` on triton. Eddie asks the web team for the deploy.
