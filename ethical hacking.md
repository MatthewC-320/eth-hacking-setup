**ethical hacking outline**

there will be three phases: an initial foothold via a vulnerable webserver hosted inside a docker container, a pivot into a *second* Docker container hosting MariaDB via two different N-day exploits, a trivial escape from that Docker container as it is privileged, and reverse engineering & exploiting a *watchdog* service running as root.

**attack path**

**the webserver**

the website is a simple flask server hosting a fanpage for the television series Serial Experiments: Lain (i was 17 when i made this ok).

three vulnerabilities must be chained in order to achieve remote code execution on the webserver, which are helpfully signposted on the website. the source code will not be made available to participants.

first, a local file inclusion vulnerability. in the website's design, a small simulated terminal reveals a snippet of the source code in ***lain\_app.py***:

            `... LINES REDACTED ...`  
            `@app.route('/search', methods=['GET'])`  
            `def search():`  
                `query = request.args.get('q', '').lower()`  
                `if not query:`  
                    `return jsonify({'error': 'No search query provided'}), 400`  
                  
                `file_path = os.path.join(files_dir, query)`  
                `try:`  
                    `file_content = open(file_path, 'r').read()`  
                    `return file_content`  
                `except:`  
                    `return "Something went wrong..."`  
            `... LINES REDACTED ...`

the endpoint is meant to display plaintext files stored in a subdirectory within the webserver's root, but uses the unsafe method **os.path.join** on an attacker-controlled GET parameter *q,* which is vulnerable to **local file inclusion**. passing ?q=../lain\_app.py allows us to retrieve the source code of the application.

some helpful snippets from the source code:

`…`  
`import bcrypt`  
`from lain_secret import secret_key`  
`import glob`  
`…`

first things first, the import statement tells us there is a submodule named *lain\_secret*, which we can also read using our local file inclusion vulnerability.

second things second, there is an error-based oracle using the **glob** module in the "disabled" *upload/* endpoint:

`def upload():`  
`if request.method == 'POST':`  
`filename = request.form["filename"]`  
`content = request.form["content"]`

`if not filename or not content:`  
`return default('error', 'filename and content are required...')`

`file_path = os.path.join(files_dir, filename)`

`if len(glob.glob(file_path)) > 0:`  
`return default('error', 'file already exists...')`

`return default('error', 'file uploading is currently disabled.')`

`return render_template('upload.html')`

because the **glob** module uses shell-based syntax, we can enumerate the existence of arbitrary files on the webserver through character-by-character iteration. say we want to find the name of a secret file. we can use the \* operator to iterate through all possible filenames and narrow down the Nth character of the filename by continually matching longer and longer prefixes. this is usually a weak vulnerability, but the *lain\_secret*.*py* file shows us that the Flask session-signing secret is stored on disk with a random hash as the filename:

`secret_file = [f for f in os.listdir('key/') if f.startswith('secret_')]`  
`if len(secret_file) == 0:`  
`secret_key = hashlib.sha256(os.urandom(32)).hexdigest()`  
`filename = 'key/secret_' + str(uuid.uuid4())`  
`with open(filename, 'w') as f:`  
`f.write(secret_key)`

combined with our local file inclusion, we can iteratively leak the filename of the secret key and then leak its contents. thus, we now can forge arbitrary session tokens as the Flask database.

third things third, forging a session token as the *lain* user provides us with an endpoint vulnerable to trivial **server-side request forgery**.

`@app.route("/lain")`  
`def lain():`  
`if not session.get("is_admin"):`  
`return redirect(url_for("login"))`

`# for Lain's debugging purposes`  
`file = open('templates/dashboard.html').read().replace('[LAIN]', session.get('uuid'))`  
`return render_template_string(file)`

this endpoint is gated behind an authentication check which we can now bypass. furthermore, it takes the *uuid* parameter of the cookie and passes it to *render\_template\_string*, which is a trivial SSTI vulnerability. there are known chains for SSTI in Flask to RCE, which shall not be elaborated on here.

in sum, we combine a local file inclusion to leak the source code of the webserver, which reveals that the secret key is stored in a randomized file at a known directory. using a globbing oracle, we can leak the randomized filename by iteratively building longer and longer prefixes. after we leak the secret key, we can forge arbitrary session tokens, bypass an authentication check, and then leverage an SSTI vulnerability in a gated endpoint for RCE.

**the database management system**

attaining RCE, we end up in a Docker container with access to the host computer's MariaDB instance; the Docker container is provided with lowly-privileged user credentials to the MariaDB with basic CRUD capabilities. the MariaDB instance is appropriately hardened – the user cannot read or execute files on the host server.

further research shows that the version of MariaDB used is vulnerable to an as-of-yet unpatched series of memory corruption exploits that result in remote code execution on the host service. a proof-of-concept as well as a (remarkably thin\!) writeup is provided by *v12* – they chain two bugs, an OOB read in multipolygons to leak PIE and a UAF in SYS\_REFCURSOR objects to overwrite a vtable pointer and get code execution.

using *v12*'s exploit script, we will discover that it **does not work.** it only works on fresh instances of MariaDB, which the host computer's service clearly is not. the exploit script must be tweaked, requiring a careful understanding of MariaDB internals, memory allocator quirks, and heap exploitation primitives to exploit.

the script is a good starting point, and should provide an open-ended exploitation path for potential solvers. it is expected that this is a significant binary exploitation problem, due to the complexity of MariaDB's heap state (and not due to the actual primitives themselves, which seem to be quite trivial). almost all of the difficulty is getting the exploit reliably firing on an as-of-yet unknown heap state, various techniques such as heap spraying, heap grooming, and heap feng shui are required.

here is an overview of the bugs, and how they are exploited:

**first bug** – OOB multipolygon read. the multipolygon object is a known datatype in MariaDB which stores geometric data for geographical purposes. a *multipolygon* is a simple object with a header that specifies N following polygons, followed by a contiguous section of *N* polygon blocks (typically, simple x,y double pairs formatted as consecutive QWORDs). a malformed multipolygon can be created which specifies N+1 polygons for only N following polygon blocks – adjacent heap data is therefore treated as polygon data. the adjacent data is leaked through an *ST\_AREA*() call, which calculates the area of each polygon and sums them. by crafting a specific polygon shape and carefully placing memory addresses in a consecutive table column (perhaps via the *BLOB* entry, stored as a heap pointer in memory) we can leak heap addresses through ST\_AREA.

**second bug** – SYS\_REFCURSOR UaF. SYS\_REFCURSOR objects are stored in dynamically-sized arrays, which are controlled through realloc(). the cap for dynamic-sizes are powers of 2: that is, if a chunk contains 31 SYS\_REFCURSORS, the 32nd addition will not cause a realloc(), but the 33rd will. the bug exists when this realloc is triggered in the same query that the cursors are used: they will refer to the smaller, already-freed chunk instead of the newly realloc'd(), bigger chunk. these chunks contain vtable pointers which are trivial to overwrite, leading to RCE via hijacked vtable pointer.

the steps to the exploit generally proceed in two phases: obtaining a PIE leak, and then triggering the UaF.

1. a heap-spray is performed to completely empty the bins. the bins in question are the unsorted and largebins, as these are the bins that will service requests for new tables and SYS\_REFCURSOR chunks.  
2. the heap is groomed. two chunks, one of size 0x2000 (a table chunk) and one of size 0xe00 (a size-16 SYS\_REFCURSOR chunk) are allocated such that they are almost (but not quite) adjacent.  
3. the two chunks are freed to populate the now empty bins. a table chunk is allocated which reclaims the first chunk, and SYS\_REFCURSOR realloc() sizing is triggered to fill the second chunk.  
4. a table chunk is now near a SYS\_REFCURSOR chunk. these SYS\_REFCURSOR chunks contain PIE pointers, and since they are near to our malformed pointer, we can similarly forge a multipolygon that contains a null-sized polygon and another that occupies just contiguous space to take the PIE pointer as part of an (x,y) pair.  
5. the SYS\_REFCURSOR realloc() is retriggered, freeing the chunk containing the PIE pointer. we immediately remainder the chunk such that all the points preceding it tend to null, and the PIE pointer's effect on the area is amplified and dominates the rest of the area calculation, resulting in an exact retrieval of the pointer. note that the chunks are not adjacent, but the remaining data in between both chunks just happen to tend to zero when parsed as null.  
6. a Call-Oriented-Programming chain is written in a BLOB entry, and then similar MULTIPOLYGON techniques are used to leak the BLOB entry (this is much easier, a BLOB pointer can be inserted directly adjacent to a malformed polygon). note that the BLOB will inevitably be freed, but some bytes of the chunk remain untouched; we use these bytes to create a sort of patchwork COP chain.  
7. the COP chain is written for *ret2syscall*. we use a series of call gadgets in the binary to carefully set the registers for an *execve()* syscall, preparing simultaneously the string arguments and pointers required.  
8. the heap is sprayed to prepare for the UaF, emptying the largebins.  
9. the SYS\_REFCURSOR UaF and vtable overwrite is triggered, dispatching into the COP chain and causing the eventual *execve()* call.

a working *exploit.py* is provided & attached.  
after this, a shell as *mysql* is attained inside a second Docker container.   
**docker escape**  
the Docker container has SYS\_ADMIN privileges and no AppArmor profile; the mysql service is running as root so we can use any one of the known docker container escapes to get out. https://blog.trailofbits.com/2019/07/19/understanding-docker-container-escapes/

**privilege escalation to root**

a simple *ps \-aux* reveals that a *watchdog* binary is running as root on the host computer. it is a compiled C binary, so it must be reverse engineered to discover its functionality. static reverse engineering reveals that it is a heartbeat service that pings the Docker services intermittently, saving the output of these pings to a .json file in /tmp/. none of this is relevant to exploitation – what is relevant is that the *watchdog* binary is listening on port 3333 internally, and exposes 4 commands through a TCP interface. commands 1 through 2 triggers the heartbeat and command 3 allows for the filename to be changed, defaulting to a random filename in the /tmp/ directory. command 3 is vulnerable to **command injection** through the filename; an attacker can specify a malicious filename with shell meta-characters that causes arbitrary command execution as root. from here, escalation is trivial.

**setup details**

the webserver should be hosted in one docker container and exposed to the external interface on port 8888\.  
the MariaDB database should be hosted in another docker container on port 7777, running with –cap SYS\_ADMIN, and exposed only to the internal interface.  
port 3333, the *watchdog* binary, should only be accessible on the host container and not through any of the docker images.

tentatively the two services are most important and can be configured with a docker-compose for now

**hardening**

\[tbd, here are my notes\]

*Do not use the \--privileged flag or mount a Docker socket inside the container. The docker socket allows for spawning containers, so it is an easy way to take full control of the host, for example, by running another container with the \--privileged flag.*  
*Do not run as root inside the container. Use a different user or user namespaces. The root in the container is the same as on host unless remapped with user namespaces. It is only lightly restricted by, primarily, Linux namespaces, capabilities, and cgroups.*  
*Drop all capabilities (--cap-drop=all) and enable only those that are required (--cap-add=...). Many of workloads don’t need any capabilities and adding them increases the scope of a potential attack.*  
*Use the “no-new-privileges” security option to prevent processes from gaining more privileges, for example through suid binaries.*  
*Limit resources available to the container. Resource limits can protect the machine from denial of service attacks.*  
*Adjust seccomp, AppArmor (or SELinux) profiles to restrict the actions and syscalls available for the container to the minimum required.*

apparmor is a good idea for both the webserver and the mariadb instance ( care should be taken to ensure that apparmor doesnt brick the mariadb exploit )

\[ here are claudes notes which are useful and correct \]

*SSH: key-only auth, no root login, fail2ban.*  
*Host firewall (ufw/iptables) allowing only 8888 (web), 3306 (internal only), and SSH from your admin IP.*  
*Automatic OS security patches for anything not part of your exploit chain (kernel, sudo, systemd CVEs)*

*Least-privilege MariaDB grants, which you've already noted (CRUD only, no FILE privilege, no SUPER). Also explicitly revoke PROCESS, RELOAD, and any privilege that could shortcut privesc.*  
*Network segmentation: the webserver container should only be able to reach MariaDB on 3306, not the rest of the host network or other containers. Use a dedicated Docker bridge network with explicit allow rules, not \--network host.*  
*MariaDB bind-address scoped to the internal Docker network, not 0.0.0.0 on the host's public interface — otherwise attackers could skip your webserver RCE entirely and hit MariaDB directly if they find the port.*

*Disable/audit MariaDB features that offer alternate RCE shortcuts: secure\_file\_priv set to block LOAD\_FILE/INTO OUTFILE, UDF loading disabled, no sys\_exec-style plugins.*

*Debug mode off, verbose errors off*  
*Security headers. Content-Security-Policy, X-Content-Type-Options: nosniff, X-Frame-Options.*

