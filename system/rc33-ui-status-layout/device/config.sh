#!/bin/sh

GKD_CONFIG_DEFAULT=${GKD_CONFIG_DEFAULT:-/etc/gkd-mini/gdkmini.conf}
GKD_CONFIG_OVERRIDE=${GKD_CONFIG_OVERRIDE:-/usr/local/etc/gkd-mini/gdkmini.conf}
GKD_CONFIG_EFFECTIVE=${GKD_CONFIG_EFFECTIVE:-/run/gkd-config/current/effective.conf}

gkd_config_select()
{
	if [ -n "${GKD_CONFIG_FILE:-}" ]; then
		GKD_CONFIG=$GKD_CONFIG_FILE
	elif [ -f "$GKD_CONFIG_EFFECTIVE" ]; then
		GKD_CONFIG=$GKD_CONFIG_EFFECTIVE
	elif [ -f "$GKD_CONFIG_OVERRIDE" ] && [ ! -L "$GKD_CONFIG_OVERRIDE" ]; then
		GKD_CONFIG=$GKD_CONFIG_OVERRIDE
	else
		GKD_CONFIG=$GKD_CONFIG_DEFAULT
	fi
	[ -f "$GKD_CONFIG" ] && [ ! -L "$GKD_CONFIG" ]
}

gkd_config_raw()
{
	key=$1 default=$2
	count=$(grep -c "^${key}=" "$GKD_CONFIG" 2>/dev/null || true)
	case "$count" in
	0) value=$default ;;
	1) value=$(sed -n "s/^${key}=//p" "$GKD_CONFIG") ;;
	*) return 1 ;;
	esac
	[ -n "$value" ] || return 1
	printf '%s\n' "$value"
}

gkd_config_uint()
{
	key=$1 default=$2 minimum=$3 maximum=$4
	value=$(gkd_config_raw "$key" "$default") || return 1
	case "$value" in ''|*[!0-9]*) return 1 ;; 0) ;; 0*) return 1 ;; esac
	[ "$value" -ge "$minimum" ] && [ "$value" -le "$maximum" ] || return 1
	printf '%s\n' "$value"
}

gkd_config_ipv4()
{
	value=$(gkd_config_raw "$1" "$2") || return 1
	old_ifs=$IFS
	IFS=.
	set -- $value
	IFS=$old_ifs
	[ "$#" -eq 4 ] || return 1
	for octet do
		case "$octet" in ''|*[!0-9]*) return 1 ;; 0) ;; 0*) return 1 ;; esac
		[ "$octet" -le 255 ] || return 1
	done
	printf '%s\n' "$value"
}

gkd_config_token()
{
	value=$(gkd_config_raw "$1" "$2") || return 1
	case "$value" in *[!A-Za-z0-9_.-]*|'') return 1 ;; esac
	[ "${#value}" -le "$3" ] || return 1
	printf '%s\n' "$value"
}
