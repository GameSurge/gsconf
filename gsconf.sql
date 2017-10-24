--
-- PostgreSQL database dump
--

SET client_encoding = 'LATIN1';
SET standard_conforming_strings = off;
SET check_function_bodies = false;
SET client_min_messages = warning;
SET escape_string_warning = off;

--
-- Name: SCHEMA public; Type: COMMENT; Schema: -; Owner: postgres
--

COMMENT ON SCHEMA public IS 'Standard public schema';


SET search_path = public, pg_catalog;

--
-- Name: ircd_oper_priv_status; Type: DOMAIN; Schema: public; Owner: gsdev
--

CREATE DOMAIN ircd_oper_priv_status AS smallint NOT NULL
    CONSTRAINT ircd_oper_priv_status_check CHECK (((VALUE >= (-1)) AND (VALUE <= 1)));


ALTER DOMAIN public.ircd_oper_priv_status OWNER TO gsdev;

--
-- Name: server_private_ip(character varying, character varying, boolean); Type: FUNCTION; Schema: public; Owner: gsdev
--

CREATE FUNCTION server_private_ip(server character varying, hub character varying, want_hub boolean) RETURNS inet
    AS $$DECLARE
server_ip inet;
server_ip_local inet;
hub_ip inet;
hub_ip_local inet;
use_local boolean;
BEGIN
use_local := true;
-- Fetch server ips
SELECT irc_ip_priv, irc_ip_priv_local
INTO server_ip, server_ip_local
FROM servers WHERE name = server;
-- Fetch hub ips
SELECT irc_ip_priv, irc_ip_priv_local
INTO hub_ip, hub_ip_local
FROM servers WHERE name = hub;

-- If one of the servers doesn't has a local IP set, use the regular ip
IF(server_ip_local ISNULL OR hub_ip_local ISNULL) THEN
  use_local := false;
-- If the local IPs aren't in the same subnet, use the regular ip
ELSIF(network(server_ip_local) != network(hub_ip_local)) THEN
  use_local := false;
END IF;

IF(use_local) THEN
  IF(want_hub) THEN
    RETURN host(hub_ip_local);
  ELSE
    RETURN host(server_ip_local);
  END IF;
ELSE
  IF(want_hub) THEN
    RETURN host(hub_ip);
  ELSE
    RETURN host(server_ip);
  END IF;
END IF;
END$$
    LANGUAGE plpgsql STABLE;


ALTER FUNCTION public.server_private_ip(server character varying, hub character varying, want_hub boolean) OWNER TO gsdev;

--
-- Name: service_private_ip(character varying, character varying, boolean); Type: FUNCTION; Schema: public; Owner: gsdev
--

CREATE FUNCTION service_private_ip(service character varying, hub character varying, want_hub boolean) RETURNS inet
    AS $$DECLARE
service_ip inet;
service_ip_local inet;
hub_ip inet;
hub_ip_local inet;
use_local boolean;
BEGIN
use_local := true;
-- Fetch service ips
SELECT ip, ip_local
INTO service_ip, service_ip_local
FROM services WHERE name = service;
-- Fetch hub ips
SELECT irc_ip_priv, irc_ip_priv_local
INTO hub_ip, hub_ip_local
FROM servers WHERE name = hub;

-- If one of the servers doesn't has a local IP set, use the regular ip
IF(service_ip_local ISNULL OR hub_ip_local ISNULL) THEN
  use_local := false;
-- If the local IPs aren't in the same subnet, use the regular ip
ELSIF(network(service_ip_local) != network(hub_ip_local)) THEN
  use_local := false;
END IF;

IF(use_local) THEN
  IF(want_hub) THEN
    RETURN host(hub_ip_local);
  ELSE
    RETURN host(service_ip_local);
  END IF;
ELSE
  IF(want_hub) THEN
    RETURN host(hub_ip);
  ELSE
    RETURN host(service_ip);
  END IF;
END IF;
END$$
    LANGUAGE plpgsql STABLE;


ALTER FUNCTION public.service_private_ip(service character varying, hub character varying, want_hub boolean) OWNER TO gsdev;

--
-- Name: valid_for_type(character varying, character varying); Type: FUNCTION; Schema: public; Owner: gsdev
--

CREATE FUNCTION valid_for_type(val character varying, typename character varying) RETURNS boolean
    AS $$BEGIN
EXECUTE 'SELECT CAST(' || quote_literal(val) || ' AS ' || typename || ')';
RETURN true;
EXCEPTION WHEN invalid_text_representation THEN
RETURN false;
END$$
    LANGUAGE plpgsql IMMUTABLE;


ALTER FUNCTION public.valid_for_type(val character varying, typename character varying) OWNER TO gsdev;

SET default_tablespace = '';

SET default_with_oids = false;

--
-- Name: clientgroups; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE clientgroups (
    name character varying(32) NOT NULL,
    server character varying(63) NOT NULL,
    connclass character varying(32) NOT NULL,
    "password" character varying(32),
    class_maxlinks integer
);


ALTER TABLE public.clientgroups OWNER TO gsdev;

--
-- Name: clients; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE clients (
    "group" character varying(32) NOT NULL,
    server character varying(63) NOT NULL,
    id serial NOT NULL,
    ident character varying(10) DEFAULT '*'::character varying,
    ip inet,
    host character varying(63),
    CONSTRAINT clients_host_check CHECK (((host)::text <> '*'::text)),
    CONSTRAINT clients_ident_check CHECK (((ident)::text <> ''::text))
);


ALTER TABLE public.clients OWNER TO gsdev;


--
-- Name: connclasses_servers; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE connclasses_servers (
    name character varying(32) NOT NULL,
    server_type character varying(5) DEFAULT '*'::character varying NOT NULL,
    pingfreq character varying(32) DEFAULT '1 minutes 30 seconds'::character varying NOT NULL,
    connectfreq character varying(32) DEFAULT '5 minutes'::character varying NOT NULL,
    maxlinks integer DEFAULT 0 NOT NULL,
    sendq integer DEFAULT 100000000 NOT NULL
);


ALTER TABLE public.connclasses_servers OWNER TO gsdev;

--
-- Name: connclasses_users; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE connclasses_users (
    name character varying(32) NOT NULL,
    pingfreq character varying(32) DEFAULT '1 minutes 30 seconds'::character varying NOT NULL,
    maxlinks integer DEFAULT 0 NOT NULL,
    sendq integer DEFAULT 655360 NOT NULL,
    recvq integer DEFAULT 1024 NOT NULL,
    usermode character varying(16),
    priv_local ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_umode_nochan ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_umode_noidle ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_umode_chserv ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_notargetlimit ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_flood ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_pseudoflood ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_gline_immune ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_die ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_restart ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_chan_limit ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    fakehost character varying(63)
);


ALTER TABLE public.connclasses_users OWNER TO gsdev;

--
-- Name: features; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE features (
    name character varying(64) NOT NULL,
    value character varying(64) NOT NULL,
    server_type character varying(5) DEFAULT '*'::character varying NOT NULL
);


ALTER TABLE public.features OWNER TO gsdev;

--
-- Name: forwards; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE forwards (
    id serial NOT NULL,
    prefix character varying(1) NOT NULL,
    target character varying(63) NOT NULL,
    server character varying(63)
);


ALTER TABLE public.forwards OWNER TO gsdev;

--
-- Name: jupes; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE jupes (
    name character varying(32) NOT NULL,
    nicks character varying(256) NOT NULL
);


ALTER TABLE public.jupes OWNER TO gsdev;

--
-- Name: jupes2servers; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE jupes2servers (
    jupe character varying(32) NOT NULL,
    server character varying(63) NOT NULL
);


ALTER TABLE public.jupes2servers OWNER TO gsdev;

--
-- Name: links; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE links (
    server character varying(63) NOT NULL,
    hub character varying(63) NOT NULL,
    autoconnect boolean DEFAULT false NOT NULL,
    port integer,
    CONSTRAINT not_self CHECK (((server)::text <> (hub)::text))
);


ALTER TABLE public.links OWNER TO gsdev;

--
-- Name: operhosts; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE operhosts (
    oper character varying(32) NOT NULL,
    mask character varying(74) NOT NULL
);


ALTER TABLE public.operhosts OWNER TO gsdev;

--
-- Name: opers; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE opers (
    name character varying(32) NOT NULL,
    username character varying(32) NOT NULL,
    "password" character varying(64) NOT NULL,
    connclass character varying(32) NOT NULL,
    active boolean DEFAULT true NOT NULL,
    priv_local ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_umode_nochan ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_umode_noidle ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_umode_chserv ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_notargetlimit ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_flood ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_pseudoflood ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_gline_immune ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_die ircd_oper_priv_status DEFAULT (-1) NOT NULL,
    priv_restart ircd_oper_priv_status DEFAULT (-1) NOT NULL
);


ALTER TABLE public.opers OWNER TO gsdev;

--
-- Name: opers2servers; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE opers2servers (
    oper character varying(32) NOT NULL,
    server character varying(63) NOT NULL
);


ALTER TABLE public.opers2servers OWNER TO gsdev;

--
-- Name: ports; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE ports (
    id serial NOT NULL,
    server character varying(63) NOT NULL,
    port integer NOT NULL,
    ip inet,
    flag_server boolean DEFAULT false NOT NULL,
    flag_hidden boolean DEFAULT false NOT NULL,
    flag_webirc boolean DEFAULT false NOT NULL
);


ALTER TABLE public.ports OWNER TO gsdev;

--
-- Name: pseudos; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE pseudos (
    id serial NOT NULL,
    command character varying(16) NOT NULL,
    name character varying(30) NOT NULL,
    target character varying(94) NOT NULL,
    prepend character varying(64),
    server character varying(63)
);


ALTER TABLE public.pseudos OWNER TO gsdev;

--
-- Name: servers; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE servers (
    name character varying(63) NOT NULL,
    "type" character varying(5) NOT NULL,
    description character varying(64),
    irc_ip_priv inet NOT NULL,
    irc_ip_priv_local inet,
    irc_ip_pub inet NOT NULL,
    "numeric" integer NOT NULL,
    contact character varying(64),
    location1 character varying(64),
    location2 character varying(64),
    provider character varying(32),
    sno_connexit boolean DEFAULT false NOT NULL,
    ssh_user character varying(32) NOT NULL,
    ssh_host character varying(63) NOT NULL,
    ssh_port integer DEFAULT 22 NOT NULL,
    link_pass character varying(64) NOT NULL,
    server_port integer NOT NULL
);


ALTER TABLE public.servers OWNER TO gsdev;

--
-- Name: servicelinks; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE servicelinks (
    service character varying(63) NOT NULL,
    hub character varying(63) NOT NULL
);


ALTER TABLE public.servicelinks OWNER TO gsdev;

--
-- Name: services; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE services (
    name character varying(63) NOT NULL,
    ip inet NOT NULL,
    ip_local inet,
    link_pass character varying(64) NOT NULL,
    flag_hub boolean DEFAULT true NOT NULL,
    flag_uworld boolean DEFAULT true NOT NULL,
    "numeric" integer NOT NULL
);


ALTER TABLE public.services OWNER TO gsdev;

--
-- Name: webirc; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE webirc (
    name character varying(32) NOT NULL,
    ip inet NOT NULL,
    "password" character varying(32) NOT NULL,
    ident character varying(10),
    hmac boolean DEFAULT false NOT NULL,
    hmac_time integer DEFAULT 30 NOT NULL,
    description character varying(32) NOT NULL
);


ALTER TABLE public.webirc OWNER TO gsdev;

--
-- Name: webirc2servers; Type: TABLE; Schema: public; Owner: gsdev; Tablespace:
--

CREATE TABLE webirc2servers (
    webirc character varying(32) NOT NULL,
    server character varying(63) NOT NULL
);


ALTER TABLE public.webirc2servers OWNER TO gsdev;

--
-- Data for Name: clientgroups; Type: TABLE DATA; Schema: public; Owner: gsdev
--



--
-- Data for Name: clients; Type: TABLE DATA; Schema: public; Owner: gsdev
--



--
-- Data for Name: connclasses_servers; Type: TABLE DATA; Schema: public; Owner: gsdev
--

INSERT INTO connclasses_servers VALUES ('HubToHub', 'HUB', '2 minutes 30 seconds', '5 minutes', 1024, 150000000);
INSERT INTO connclasses_servers VALUES ('HubToLeaf', 'HUB', '1 minutes 30 seconds', '5 minutes', 0, 100000000);
INSERT INTO connclasses_servers VALUES ('LeafToHub', 'LEAF', '1 minutes 30 seconds', '2 minutes 30 seconds', 1, 100000000);
INSERT INTO connclasses_servers VALUES ('LeafToHub', 'STAFF', '1 minutes 30 seconds', '2 minutes 30 seconds', 1, 100000000);
INSERT INTO connclasses_servers VALUES ('LeafToHub', 'BOTS', '1 minutes 30 seconds', '2 minutes 30 seconds', 1, 100000000);
INSERT INTO connclasses_servers VALUES ('HubToService', 'HUB', '1 minutes 30 seconds', '5 minutes', 0, 15000000);


--
-- Data for Name: connclasses_users; Type: TABLE DATA; Schema: public; Owner: gsdev
--

INSERT INTO connclasses_users VALUES ('Users', '1 minutes 30 seconds', 0, 655360, 1024, 'iw', -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, NULL);
INSERT INTO connclasses_users VALUES ('TrialServerAdmins', '1 minutes 30 seconds', 0, 6553600, 10240, 'iw', 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, NULL);
INSERT INTO connclasses_users VALUES ('Opers', '1 minutes 30 seconds', 0, 6553600, 20480, 'iw', 0, -1, -1, -1, -1, -1, -1, -1, 0, 0, -1, NULL);
INSERT INTO connclasses_users VALUES ('SeniorOpers', '1 minutes 30 seconds', 0, 6553600, 40960, 'iw', 0, -1, -1, -1, -1, 1, -1, -1, 0, 0, -1, NULL);
INSERT INTO connclasses_users VALUES ('NetOps', '2 minutes 30 seconds', 0, 65536000, 40960, 'iw', 0, -1, -1, 1, -1, 1, -1, -1, 0, 0, -1, NULL);
INSERT INTO connclasses_users VALUES ('Staff', '1 minutes 30 seconds', 0, 655360, 10240, 'iwx', -1, 1, -1, -1, 1, -1, -1, -1, -1, -1, 1, 'staff.lameircnet');
INSERT INTO connclasses_users VALUES ('Bots', '1 minutes 30 seconds', 0, 1048576, 1024, 'iw', -1, 1, -1, -1, 1, 1, -1, -1, -1, -1, 1, NULL);
INSERT INTO connclasses_users VALUES ('Bots-NoIdle', '1 minutes 30 seconds', 0, 1048576, 1024, 'iw', -1, 1, 1, -1, 1, 1, -1, -1, -1, -1, 1, NULL);
INSERT INTO connclasses_users VALUES ('Bots-ChServ', '1 minutes 30 seconds', 0, 1048576, 1024, 'iw', -1, 1, 1, 1, 1, 1, -1, -1, -1, -1, 1, NULL);


--
-- Data for Name: features; Type: TABLE DATA; Schema: public; Owner: gsdev
--

INSERT INTO features VALUES ('HUB', 'TRUE', 'HUB');
INSERT INTO features VALUES ('RELIABLE_CLOCK', 'TRUE', 'HUB');
INSERT INTO features VALUES ('IPCHECK_CLONE_LIMIT', '400', 'STAFF');
INSERT INTO features VALUES ('IPCHECK_CLONE_LIMIT', '400', 'BOTS');
INSERT INTO features VALUES ('IPCHECK_CLONE_PERIOD', '400', 'STAFF');
INSERT INTO features VALUES ('IPCHECK_CLONE_PERIOD', '400', 'BOTS');
INSERT INTO features VALUES ('RELIABLE_CLOCK', 'FALSE', '*');
INSERT INTO features VALUES ('BUFFERPOOL', '64000000', '*');
INSERT INTO features VALUES ('DEFAULT_LIST_PARAM', '>40', '*');
INSERT INTO features VALUES ('HIDDEN_HOST', 'user.lameircnet', '*');
INSERT INTO features VALUES ('HIDDEN_IP', '127.0.0.1', '*');
INSERT INTO features VALUES ('MAXCHANNELSPERUSER', '20', '*');
INSERT INTO features VALUES ('CONNECTTIMEOUT', '15', '*');
INSERT INTO features VALUES ('CONFIG_OPERCMDS', 'TRUE', '*');
INSERT INTO features VALUES ('OPLEVELS', 'FALSE', '*');
INSERT INTO features VALUES ('ZANNELS', 'FALSE', '*');
INSERT INTO features VALUES ('ANNOUNCE_INVITES', 'TRUE', '*');
INSERT INTO features VALUES ('HIS_STATS_w', 'TRUE', '*');
INSERT INTO features VALUES ('HIS_REMOTE', '0', '*');
INSERT INTO features VALUES ('HIS_MODEWHO', 'FALSE', '*');
INSERT INTO features VALUES ('HIS_SERVERNAME', '*.Lame-IRC-Network.net', '*');
INSERT INTO features VALUES ('HIS_SERVERINFO', 'The Most Lame IRC Network On The Planet', '*');
INSERT INTO features VALUES ('TOPIC_BURST', 'TRUE', '*');
INSERT INTO features VALUES ('URL_CLIENTS', 'http://www.lame-irc-network.net', '*');
INSERT INTO features VALUES ('URLREG', 'http://www.lame-irc-network.net', '*');
INSERT INTO features VALUES ('HIS_URLSERVERS', 'http://www.lame-irc-network.net', '*');
INSERT INTO features VALUES ('MAXCHANNELSPERUSER', '40', 'STAFF');
INSERT INTO features VALUES ('NICKLEN', '30', '*');
INSERT INTO features VALUES ('NETWORK', 'LameIRCNetwork', '*');


--
-- Data for Name: forwards; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: jupes; Type: TABLE DATA; Schema: public; Owner: gsdev
--

INSERT INTO jupes VALUES ('Services', 'NickServ,ChanServ,OperServ,MemoServ,HelpServ,RegServ,AuthServ,OpServ,Global,SpamServ');
INSERT INTO jupes VALUES ('Services2', 'NickServX,ChanServX,OperServX,MemoServX,HelpServX,AuthServX,OpServX,GlobalX,SpamServX');
INSERT INTO jupes VALUES ('Phishing', 'login,pass,newpass,org,auth');
INSERT INTO jupes VALUES ('Bots', 'IRPG,HostServ');
INSERT INTO jupes VALUES ('OneLetter', 'A,B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T,U,V,W,X,Y,Z,{,|,},~,-,_,`');


--
-- Data for Name: jupes2servers; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: links; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: operhosts; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: opers; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: opers2servers; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: ports; Type: TABLE DATA; Schema: public; Owner: gsdev
--



--
-- Data for Name: pseudos; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: servers; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: servicelinks; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: services; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: webirc; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Data for Name: webirc2servers; Type: TABLE DATA; Schema: public; Owner: gsdev
--


--
-- Name: clientgroups_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY clientgroups
    ADD CONSTRAINT clientgroups_pkey PRIMARY KEY (name, server);


--
-- Name: clients_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY clients
    ADD CONSTRAINT clients_pkey PRIMARY KEY (id);


--
-- Name: connclasses_servers_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY connclasses_servers
    ADD CONSTRAINT connclasses_servers_pkey PRIMARY KEY (name, server_type);


--
-- Name: connclasses_users_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY connclasses_users
    ADD CONSTRAINT connclasses_users_pkey PRIMARY KEY (name);


--
-- Name: features_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY features
    ADD CONSTRAINT features_pkey PRIMARY KEY (name, server_type);


--
-- Name: forwards_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY forwards
    ADD CONSTRAINT forwards_pkey PRIMARY KEY (id);


--
-- Name: forwards_prefix_key; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY forwards
    ADD CONSTRAINT forwards_prefix_key UNIQUE (prefix, server);


--
-- Name: jupes2servers_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY jupes2servers
    ADD CONSTRAINT jupes2servers_pkey PRIMARY KEY (jupe, server);


--
-- Name: jupes_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY jupes
    ADD CONSTRAINT jupes_pkey PRIMARY KEY (name);


--
-- Name: links_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY links
    ADD CONSTRAINT links_pkey PRIMARY KEY (server, hub);


--
-- Name: operhosts_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY operhosts
    ADD CONSTRAINT operhosts_pkey PRIMARY KEY (oper, mask);


--
-- Name: opers2servers_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY opers2servers
    ADD CONSTRAINT opers2servers_pkey PRIMARY KEY (oper, server);


--
-- Name: opers_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY opers
    ADD CONSTRAINT opers_pkey PRIMARY KEY (name);


--
-- Name: ports_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY ports
    ADD CONSTRAINT ports_pkey PRIMARY KEY (id);


--
-- Name: pseudos_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY pseudos
    ADD CONSTRAINT pseudos_pkey PRIMARY KEY (id);


--
-- Name: servers_numeric_key; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY servers
    ADD CONSTRAINT servers_numeric_key UNIQUE ("numeric");


--
-- Name: servers_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY servers
    ADD CONSTRAINT servers_pkey PRIMARY KEY (name);


--
-- Name: servicelinks_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY servicelinks
    ADD CONSTRAINT servicelinks_pkey PRIMARY KEY (service, hub);


--
-- Name: services_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY services
    ADD CONSTRAINT services_pkey PRIMARY KEY (name);


--
-- Name: webirc2servers_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY webirc2servers
    ADD CONSTRAINT webirc2servers_pkey PRIMARY KEY (webirc, server);


--
-- Name: webirc_pkey; Type: CONSTRAINT; Schema: public; Owner: gsdev; Tablespace:
--

ALTER TABLE ONLY webirc
    ADD CONSTRAINT webirc_pkey PRIMARY KEY (name);


--
-- Name: clients_cgroup_key; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX clients_cgroup_key ON clients USING btree ("group", server);


--
-- Name: connclasses_servers_server_type; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX connclasses_servers_server_type ON connclasses_servers USING btree (server_type);


--
-- Name: features_server_type; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX features_server_type ON features USING btree (server_type);


--
-- Name: fki_; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX fki_ ON links USING btree (port);


--
-- Name: forwards_server; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX forwards_server ON forwards USING btree (server);


--
-- Name: jupes2servers_server; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX jupes2servers_server ON jupes2servers USING btree (server);


--
-- Name: opers2servers_server; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX opers2servers_server ON opers2servers USING btree (server);


--
-- Name: pseudos_command_server; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE UNIQUE INDEX pseudos_command_server ON pseudos USING btree (command, (COALESCE(server, '*'::character varying)));


--
-- Name: pseudos_server; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX pseudos_server ON pseudos USING btree (server);


--
-- Name: servers_name_key; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE UNIQUE INDEX servers_name_key ON servers USING btree (lower((name)::text));


--
-- Name: webirc2servers_server; Type: INDEX; Schema: public; Owner: gsdev; Tablespace:
--

CREATE INDEX webirc2servers_server ON webirc2servers USING btree (server);


--
-- Name: clientgroups_connclass_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY clientgroups
    ADD CONSTRAINT clientgroups_connclass_fkey FOREIGN KEY (connclass) REFERENCES connclasses_users(name) ON UPDATE CASCADE ON DELETE RESTRICT;


--
-- Name: clientgroups_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY clientgroups
    ADD CONSTRAINT clientgroups_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: clients_cgroup_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY clients
    ADD CONSTRAINT clients_cgroup_fkey FOREIGN KEY ("group", server) REFERENCES clientgroups(name, server) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: forwards_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY forwards
    ADD CONSTRAINT forwards_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: jupes2servers_jupe_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY jupes2servers
    ADD CONSTRAINT jupes2servers_jupe_fkey FOREIGN KEY (jupe) REFERENCES jupes(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: jupes2servers_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY jupes2servers
    ADD CONSTRAINT jupes2servers_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: links_hub_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY links
    ADD CONSTRAINT links_hub_fkey FOREIGN KEY (hub) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: links_port_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY links
    ADD CONSTRAINT links_port_fkey FOREIGN KEY (port) REFERENCES ports(id) ON UPDATE CASCADE ON DELETE RESTRICT;


--
-- Name: links_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY links
    ADD CONSTRAINT links_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: operhosts_oper_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY operhosts
    ADD CONSTRAINT operhosts_oper_fkey FOREIGN KEY (oper) REFERENCES opers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: opers2servers_oper_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY opers2servers
    ADD CONSTRAINT opers2servers_oper_fkey FOREIGN KEY (oper) REFERENCES opers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: opers2servers_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY opers2servers
    ADD CONSTRAINT opers2servers_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: opers_connclass_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY opers
    ADD CONSTRAINT opers_connclass_fkey FOREIGN KEY (connclass) REFERENCES connclasses_users(name) ON UPDATE CASCADE ON DELETE RESTRICT;


--
-- Name: ports_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY ports
    ADD CONSTRAINT ports_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: pseudos_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY pseudos
    ADD CONSTRAINT pseudos_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: servicelinks_hub_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY servicelinks
    ADD CONSTRAINT servicelinks_hub_fkey FOREIGN KEY (hub) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: servicelinks_service_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY servicelinks
    ADD CONSTRAINT servicelinks_service_fkey FOREIGN KEY (service) REFERENCES services(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: webirc2servers_server_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY webirc2servers
    ADD CONSTRAINT webirc2servers_server_fkey FOREIGN KEY (server) REFERENCES servers(name) ON UPDATE CASCADE ON DELETE CASCADE;


--
-- Name: webirc2servers_webirc_fkey; Type: FK CONSTRAINT; Schema: public; Owner: gsdev
--

ALTER TABLE ONLY webirc2servers
    ADD CONSTRAINT webirc2servers_webirc_fkey FOREIGN KEY (webirc) REFERENCES webirc(name) ON UPDATE CASCADE ON DELETE CASCADE;



--
-- PostgreSQL database dump complete
--

