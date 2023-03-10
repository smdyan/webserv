# include "main.hpp"

void showCGIEnv(std::map<std::string, std::string> & envmap) {

    std::cout << std::endl << std::endl;
    std::cout << YELLOW <<  "   CGI ENVIRONNEMENT VARIABLES -----------" << END << std::endl << std::endl;
    std::map<std::string, std::string>::iterator it = envmap.begin();
	while (it != envmap.end()) {
		if (!it->second.empty()) std::cout << "      " << it->first << "=" << it->second << std::endl;
        else std::cout << "      " << it->first << "=(empty value)" << std::endl;
		++it;
	}
    std::cout << std::endl << YELLOW << "    ----------------------------- END " << END << std::endl << std::endl;
    std::cout << std::endl << std::endl;

}

/*
** GATEWAY_INTERFACE       : CGI Version
** SERVER_PROTOCOL         : Protocole
** SERVER_SOFTWARE         : Serveur
** REQUEST_URI             : URI demandée
** REQUEST_METHOD          : Type de requête
** REMOTE_ADDR             : IP du client
** PATH_INFO               : Partie de l'URI entre le nom du CGI (exclus) et le reste de l'URI (voir Wikipédia) ) - à vérifier
** PATH_TRANSLATED         : getcwd + root (sans le .) + uri
** CONTENT_LENGTH          : The length of the query information. It's available only for POST requests
** QUERY_STRING            : The URL-encoded information that is sent with GET method request.
** CONTENT_TYPE            : Type MIME des données véhiculées dans la requête
** SCRIPT_NAME             : Le chemin virtuel vers le script étant exécuté. Exemple : « /cgi-bin/script.cgi »
** REDIRECT_STATUS         : To make php-cgi work, idkw it required it
** Toutes les variables qui sont envoyées par le client sont aussi passées au script CGI,
** après que le serveur a rajouté le préfixe « HTTP_ »

*/

char ** Response::buildCGIEnv(Request * req) {

    char **env;
    size_t pos;
    std::pair<std::string, std::string> tmp;
    std::map<std::string, std::string> envmap;
    std::map<std::string, std::string> hdmap;

    /* 1) CGI INFORMATIONS */
    envmap["GATEWAY_INTERFACE"]   = "CGI/1.1";
	envmap["SERVER_PROTOCOL"]     = "HTTP/1.1";
	envmap["SERVER_SOFTWARE"]     = "webserv";
	envmap["REQUEST_URI"]         = req->uri; 
	envmap["REQUEST_METHOD"]      = req->method;
	envmap["REMOTE_ADDR"]         = req->client->ip;
	envmap["PATH_INFO"]           = req->uri;
    envmap["PATH_TRANSLATED"]     = ft::getcwdString() + req->reqLocation->root.substr(1, req->reqLocation->root.size()) + req->uri.substr(req->uri.find("/" + req->isolateFileName));
    envmap["CONTENT_LENGTH"]      = ft::to_string(req->_reqBody.size());
	envmap["QUERY_STRING"]        = req->uriQueries.empty() ? "" : req->uriQueries;
	if (!req->contentType.empty()) envmap["CONTENT_TYPE"] = req->contentType;
    envmap["SCRIPT_NAME"]         = getCGIType(req) == TESTER_CGI ? req->reqLocation->cgi : req->reqLocation->php;
 //   if (req->cgiType == TESTER_CGI && PLATFORM == "Linux" && envmap["SCRIPT_NAME"].find(".pl") == std::string::npos) {
 //       envmap["SCRIPT_NAME"] = envmap["SCRIPT_NAME"].replace(envmap["SCRIPT_NAME"].find("cgi_tester"), sizeof("cgi_tester"), "ubuntu_") + "cgi_tester";
 //   }

	envmap["SERVER_NAME"]         = "127.0.0.1";
	envmap["SERVER_PORT"]         = ft::to_string(req->client->server->port);
    if (!req->authorization.empty()) {
		pos = req->authorization.find(" ");
        if (pos != std::string::npos) {
            envmap["AUTH_TYPE"] = req->authorization.substr(0, pos);
            envmap["REMOTE_USER"] = req->authorization.substr(pos + 1);
        }
	}
    if (req->cgiType == PHP_CGI) envmap["REDIRECT_STATUS"] = "200";
    /* 2) REQUEST HEADERS PASSED TO CGI */
    hdmap = req->mapReqHeaders();
	std::map<std::string, std::string>::iterator it = hdmap.begin();
	while (it != hdmap.end()) {
		if (!it->second.empty() && it->second != "-1")
			envmap["HTTP_" + it->first] = it->second;
		++it;
	}
    hdmap.clear();
    if (SILENTLOGS == 0)
        showCGIEnv(envmap);
    int i = -1;
    env = (char**)malloc(sizeof(char*) * (envmap.size() + 1));
    it = envmap.begin();
    while (it != envmap.end()) {
        env[++i] = (char*)strdup((it->first + "=" + it->second).c_str());
		it++;
	}
    env[++i] = 0;
    envmap.clear();
    return env;
}

void clearCGI(char **args, char **env) {
    utils::strTabFree(args);
    utils::strTabFree(env);
    args = env = NULL;
}

/*
**  Main CGI Handler, called with GET/POST
**  1. Find the executable path of the CGI depending the configuration file
**  2. Build CGI environnement (passed to execve)
**  3. Build execve 2nd arguments (array of string with path to exec and arguments)
**  4. Pipe / Fork / Excve / Dup2 to write output to file and make CGI read the POST Body
**  5. In the parent, write BODY in STDIN of child with tube 
**  6. Then, parse the CGI output, knowing that its output format will always be (whatever the CGI is) : headers ... \r\n\r\n ... body
**
**  Not remember process communication ? --> http://www.zeitoun.net/articles/communication-par-tuyau/start
**  NOCLASSLOGPRINT(DEBUG, "DEBUG 2 - executable = " + executable + " and file = " + req->file);
**  NOCLASSLOGPRINT(DEBUG, ("DEBUG 3 ---> 0 = " + std::string(args[0]) + " 1 = " + std::string(args[1])));
**
**  The CGI differs depending the OS, so on Linux, we replace the name of cgi with ubuntu_ before
*/

void Response::execCGI(Request * req) {

    int     ret = -1;
    int     wRet = -1;
    int     tmpFd = -1;
    char    **args = NULL;
    char    **env = NULL;
    int     tubes[2];
    int     status;
	struct stat	buffer;
    std::string executable;
    pid_t pid;
    
    executable.clear();
    if (req->cgiType == TESTER_CGI)
        executable = req->reqLocation->cgi;
    else if (req->cgiType == PHP_CGI)
        executable = req->reqLocation->php;
    else return LOGPRINT(LOGERROR, this, ("Request::execCGI() : Internal Error - If we reach execCGI(), the cgi should be TESTER_CGI or PHP_CGI"));
 //   if (req->cgiType == TESTER_CGI && PLATFORM == "Linux" && executable.find(".pl") == std::string::npos) {
 //       executable = executable.replace(executable.find("cgi_tester"), sizeof("cgi_tester"), "ubuntu_") + "cgi_tester";
 //   }
 //   if (PLATFORM == "Linux" && req->cgiType == PHP_CGI)
 //       executable.erase(executable.find("local/"), sizeof("local/") - 1); 

    env = buildCGIEnv(req);
    args = (char **)(malloc(sizeof(char*) * 3));
    args[0] = strdup(executable.c_str());
    args[1] = strdup(req->file.c_str());
    args[2] = 0;
    if (stat(executable.c_str(), &buffer) != 0 || !(buffer.st_mode & S_IFREG)) {
        clearCGI(args, env);
        return LOGPRINT(LOGERROR, this, ("Request::execCGI() : The CGI provided in the configuration file isn't executable (path = " + executable));
    }
    if ((tmpFd = open(CGI_OUTPUT_TMPFILE, O_WRONLY | O_CREAT, 0666)) == -1) {
        clearCGI(args, env);
        LOGPRINT(LOGERROR, this, ("Request::execCGI() : open(./www/tmpFile) failed - Internal Error 500"));
        return setErrorParameters(Response::ERROR, INTERNAL_ERROR_500);
    }
    NOCLASSLOGPRINT(INFO, ("Request::execCGI() : We will fork and perform the cgi, with execve() receiving args[0] = " + std::string(args[0]) + " and args[1] = " + std::string(args[1])));
    pipe(tubes);
    if (req->method == "GET")
        close(tubes[SIDE_IN]);
    if ((pid = fork()) == 0) {
        dup2(tubes[SIDE_OUT], STDIN);   // Pour POST uniquement de façon à ce que le CGI récupère l'informations dans son STDIN, mais requis pour GET même s'il y a pas de body (autrement le cgi_tester freeze apparement)
        dup2(tmpFd, STDOUT);            // On veut que la sortie du CGI soit dirigée vers le fichier CGI_OUTPUT_TMPFILE
        ret = execve(executable.c_str(), args, env);
        exit(ret);
    } else {
        if (req->method == "POST") {
            wRet = write(tubes[SIDE_IN], req->_reqBody.c_str(), req->_reqBody.size());
            if (wRet <= 0)
                LOGPRINT(INFO, this, ("Response::execCGI() : Writing Body into STDIN child has return wRet = " + ft::to_string(wRet)));
            close(tubes[SIDE_IN]);
        }
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            ret = WEXITSTATUS(status);
            LOGPRINT(INFO, this, ("Request::execCGI() : execve() with CGI has succeed and returned : " + ft::to_string(ret)));
        } else {
            LOGPRINT(LOGERROR, this, ("Request::execCGI() : execve() with CGI has failed an return -1 - Internal Error 500"));
            setErrorParameters(Response::ERROR, INTERNAL_ERROR_500);
        }
        close(tubes[SIDE_OUT]);
        close(tmpFd);
    }
    _didCGIPassed = true;
    handleCGIOutput(req->cgiType);
    LOGPRINT(INFO, this, ("Request::execCGI() : END of execCGI()."));
    clearCGI(args, env);
}

void Response::handleCGIOutput(int cgiType) {

    int cgiFdOutput = -1;
    struct stat sb;
    std::string buffer;

    buffer.clear();
    if ((cgiFdOutput = open(CGI_OUTPUT_TMPFILE, O_RDONLY)) == -1)
        return NOCLASSLOGPRINT(LOGERROR, ("Response::handleCGIOutput() : Open() has failed while opening cgiFdOutput"));
    fstat(cgiFdOutput, &sb);
    buffer.resize(sb.st_size);

    if (read(cgiFdOutput, const_cast<char*>(buffer.data()), sb.st_size) == -1)
        NOCLASSLOGPRINT(LOGERROR, ("Response::handleCGIOutput() : read() has return -1 on CGI_OUTPUT_TMPFILE"));
    
    close(cgiFdOutput);
    _cgiOutputBody = buffer;
    if (_cgiOutputBody.find("\r\n\r\n") == std::string::npos) {
        LOGPRINT(LOGERROR, this, ("Response::handleCGIOutput() : CGI (type : " + ft::to_string(cgiType) + ") output doesn't contain <CR><LR><CR><LR> pattern. Invalid CGI. Internal Error"));
        return setErrorParameters(Response::ERROR, INTERNAL_ERROR_500);
    } else parseCGIOutput(cgiType, buffer);
    unlink(CGI_OUTPUT_TMPFILE);
    buffer.clear();
    _cgiOutputBody.clear();

}
void Response::parseCGIOutput(int cgiType, std::string & buffer) {

    size_t pos;
    size_t endLine;
    std::string key;
    std::string value;
    std::string headersSection;
    
    if (cgiType == PHP_CGI)
        _statusCode = OK_200;
    headersSection = buffer.substr(0, buffer.find("\r\n\r\n") + 1);
    if ((pos = headersSection.find("Status")) != std::string::npos) {
        pos += 8;
        endLine = headersSection.find("\r", pos);
        if (endLine == std::string::npos) endLine = headersSection.find("\n", pos);
        _statusCode = std::atoi(headersSection.substr(pos, endLine).c_str());
    }
    pos = headersSection.find("Content-Type");
    if (pos == std::string::npos) pos = headersSection.find("Content-type");
    if (pos != std::string::npos) {
        pos += 14;
        endLine = headersSection.find("\r", pos);
        if (endLine == std::string::npos) endLine = headersSection.find("\n", pos);
        contentType[0] = headersSection.substr(pos, endLine);
    }
    pos = endLine = 0;
    pos = buffer.find("\r\n\r\n") + 4;
    if (pos == std::string::npos) return NOCLASSLOGPRINT(LOGERROR, "Response::parseCGIHeadersOutput: Invalid CGI Output, not <CR><LF><CR><LF> present to separate headers from body");
    _resBody = responseUtils::setBodyNoFile(buffer.substr(pos), buffer.substr(pos).size(), contentLength);
    _resFile.clear();

}


/*
** Get CGI type depending the request and server configuration
*/

int Response::getCGIType(Request * req) {

    if (!req->reqLocation->ext.empty() && !req->reqLocation->cgi.empty() && req->uri.find(req->reqLocation->ext) != std::string::npos)
        return (TESTER_CGI);
    else if (!req->reqLocation->php.empty() && utils::isExtension(req->file, ".php"))
        return (PHP_CGI);
    else
        return (NO_CGI);
}