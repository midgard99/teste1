// -----------------------------------------------------------------------------------------
// <copyright file="cloud_storage_account.cpp" company="Microsoft">
//    Copyright 2013 Microsoft Corporation
//
//    Licensed under the Apache License, Version 2.0 (the "License");
//    you may not use this file except in compliance with the License.
//    You may obtain a copy of the License at
//      http://www.apache.org/licenses/LICENSE-2.0
//
//    Unless required by applicable law or agreed to in writing, software
//    distributed under the License is distributed on an "AS IS" BASIS,
//    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//    See the License for the specific language governing permissions and
//    limitations under the License.
// </copyright>
// -----------------------------------------------------------------------------------------

#include "stdafx.h"

#include "wascore/util.h"
#include "was/blob.h"
#include "was/queue.h"
#include "was/table.h"
#include "was/storage_account.h"
#include "wascore/resources.h"

namespace azure { namespace storage {

    const utility::string_t use_development_storage_setting_string(U("UseDevelopmentStorage"));
    const utility::string_t use_development_storage_setting_value(U("true"));
    const utility::string_t development_storage_proxy_uri_setting_string(U("DevelopmentStorageProxyUri"));
    const utility::string_t default_endpoints_protocol_setting_string(U("DefaultEndpointsProtocol"));
    const utility::string_t account_name_setting_string(U("AccountName"));
    const utility::string_t account_key_setting_string(U("AccountKey"));
    const utility::string_t blob_endpoint_setting_string(U("BlobEndpoint"));
    const utility::string_t queue_endpoint_setting_string(U("QueueEndpoint"));
    const utility::string_t table_endpoint_setting_string(U("TableEndpoint"));
    const utility::string_t endpoint_suffix_setting_string(U("EndpointSuffix"));
    const utility::string_t shared_access_signature_setting_string(U("SharedAccessSignature"));
    const utility::string_t devstore_account_name(U("devstoreaccount1"));
    const utility::string_t devstore_account_key(U("Eby8vdM02xNOcqFlqUwJPLlmEtlCDXJ1OUzFT50uSRZ6IFsuFq2UVErCz4I6tq/K1SZFPTOtr/KBHBeksoGMGw=="));
    const utility::string_t secondary_location_account_suffix(U("-secondary"));
    const utility::string_t default_endpoint_suffix(U("core.windows.net"));
    const utility::string_t default_blob_hostname_prefix(U("blob"));
    const utility::string_t default_queue_hostname_prefix(U("queue"));
    const utility::string_t default_table_hostname_prefix(U("table"));

    storage_uri construct_default_endpoint(const utility::string_t& scheme, const utility::string_t& account_name, const utility::string_t& hostname_prefix, const utility::string_t& endpoint_suffix)
    {
        utility::ostringstream_t primary;
        primary << scheme << U("://") << account_name << U('.') << hostname_prefix << U('.') << endpoint_suffix;

        utility::ostringstream_t secondary;
        secondary << scheme << U("://") << account_name << secondary_location_account_suffix << U('.') << hostname_prefix << U('.') << endpoint_suffix;

        return storage_uri(web::http::uri(primary.str()), web::http::uri(secondary.str()));
    }

    void cloud_storage_account::initialize_default_endpoints(bool use_https)
    {
        auto endpoint_suffix = m_endpoint_suffix.empty() ? default_endpoint_suffix : m_endpoint_suffix;
        const utility::string_t scheme(use_https ? U("https") : U("http"));
        m_blob_endpoint = construct_default_endpoint(scheme, m_credentials.account_name(), default_blob_hostname_prefix, endpoint_suffix);
        m_queue_endpoint = construct_default_endpoint(scheme, m_credentials.account_name(), default_queue_hostname_prefix, endpoint_suffix);
        m_table_endpoint = construct_default_endpoint(scheme, m_credentials.account_name(), default_table_hostname_prefix, endpoint_suffix);
    }

    cloud_storage_account cloud_storage_account::get_development_storage_account(const web::http::uri& proxy_uri)
    {
        web::http::uri_builder builder;
        if (proxy_uri.is_empty())
        {
            builder.set_scheme(U("http"));
            builder.set_host(U("127.0.0.1"));
        }
        else
        {
            builder.set_scheme(proxy_uri.scheme());
            builder.set_host(proxy_uri.host());
        }

        builder.set_path(devstore_account_name);

        builder.set_port(10000);
        web::uri blob_endpoint_primary = builder.to_uri();
        builder.set_port(10001);
        web::uri queue_endpoint_primary = builder.to_uri();
        builder.set_port(10002);
        web::uri table_endpoint_primary = builder.to_uri();

        builder.set_path(devstore_account_name + secondary_location_account_suffix);

        builder.set_port(10000);
        web::uri blob_endpoint_secondary = builder.to_uri();
        builder.set_port(10001);
        web::uri queue_endpoint_secondary = builder.to_uri();
        builder.set_port(10002);
        web::uri table_endpoint_secondary = builder.to_uri();

        cloud_storage_account account(storage_credentials(devstore_account_name, devstore_account_key),
            storage_uri(std::move(blob_endpoint_primary), std::move(blob_endpoint_secondary)),
            storage_uri(std::move(queue_endpoint_primary), std::move(queue_endpoint_secondary)),
            storage_uri(std::move(table_endpoint_primary), std::move(table_endpoint_secondary)));
        
        account.m_is_development_storage_account = true;
        account.m_settings.insert(std::make_pair(use_development_storage_setting_string, use_development_storage_setting_value));
        if (!proxy_uri.is_empty())
        {
            account.m_settings.insert(std::make_pair(development_storage_proxy_uri_setting_string, proxy_uri.to_string()));
        }
        
        return account;
    }

    cloud_storage_account cloud_storage_account::development_storage_account()
    {
        return get_development_storage_account(web::http::uri());
    }

    std::map<utility::string_t, utility::string_t> parse_string_into_settings(const utility::string_t& connection_string)
    {
        std::map<utility::string_t, utility::string_t> settings;
        auto splitted_string = core::string_split(connection_string, U(";"));
        
        for (auto iter = splitted_string.cbegin(); iter != splitted_string.cend(); ++iter)
        {
            if (!iter->empty())
            {
                auto equals = iter->find(U('='));

                utility::string_t key = iter->substr(0, equals);
                if (!key.empty())
                {
                    utility::string_t value;
                    if (equals != utility::string_t::npos)
                    {
                        value = iter->substr(equals + 1);
                    }

                    settings.insert(std::make_pair(std::move(key), std::move(value)));
                }
                else
                {
                    throw std::logic_error(protocol::error_invalid_settings_form);
                }
            }
        }
        
        return settings;
    }

    bool get_setting(std::map<utility::string_t, utility::string_t>& settings, const utility::string_t& key, utility::string_t& value)
    {
        auto iter = settings.find(key);
        if (iter == settings.end())
        {
            return false;
        }

        value = iter->second;
        settings.erase(iter);
        return true;
    }

    storage_credentials get_credentials(std::map<utility::string_t, utility::string_t>& settings)
    {
        utility::string_t account_name;
        utility::string_t account_key;
        utility::string_t shared_access_signature;

        get_setting(settings, account_name_setting_string, account_name);
        get_setting(settings, account_key_setting_string, account_key);
        get_setting(settings, shared_access_signature_setting_string, shared_access_signature);

        if (!account_name.empty() && !account_key.empty() && shared_access_signature.empty())
        {
            return storage_credentials(account_name, account_key);
        }

        if (account_name.empty() && account_key.empty() && !shared_access_signature.empty())
        {
            return storage_credentials(shared_access_signature);
        }

        return storage_credentials();
    }

    cloud_storage_account cloud_storage_account::parse_devstore_settings(std::map<utility::string_t, utility::string_t> settings)
    {
        utility::string_t devstore;
        if (get_setting(settings, use_development_storage_setting_string, devstore))
        {
            if (devstore != use_development_storage_setting_value)
            {
                throw std::invalid_argument(utility::conversions::to_utf8string(use_development_storage_setting_string));
            }

            utility::string_t devstore_proxy_uri;
            web::http::uri proxy_uri;
            if (get_setting(settings, development_storage_proxy_uri_setting_string, devstore_proxy_uri))
            {
                proxy_uri = web::http::uri(devstore_proxy_uri);
            }

            if (settings.empty())
            {
                return get_development_storage_account(proxy_uri);
            }
        }

        return cloud_storage_account();
    }
    
    cloud_storage_account cloud_storage_account::parse_defaults_settings(std::map<utility::string_t, utility::string_t> settings)
    {
        utility::string_t scheme;
        utility::string_t account_name;
        utility::string_t account_key;

        if (get_setting(settings, default_endpoints_protocol_setting_string, scheme) &&
            get_setting(settings, account_name_setting_string, account_name) &&
            get_setting(settings, account_key_setting_string, account_key))
        {
            utility::string_t endpoint_suffix;
            if (!get_setting(settings, endpoint_suffix_setting_string, endpoint_suffix))
            {
                endpoint_suffix = default_endpoint_suffix;
            }

            utility::string_t blob_endpoint;
            utility::string_t queue_endpoint;
            utility::string_t table_endpoint;
            get_setting(settings, blob_endpoint_setting_string, blob_endpoint);
            get_setting(settings, queue_endpoint_setting_string, queue_endpoint);
            get_setting(settings, table_endpoint_setting_string, table_endpoint);

            if (settings.empty())
            {
                cloud_storage_account account(storage_credentials(account_name, account_key),
                    blob_endpoint.empty() ? construct_default_endpoint(scheme, account_name, default_blob_hostname_prefix, endpoint_suffix) : storage_uri(web::http::uri(blob_endpoint)),
                    queue_endpoint.empty() ? construct_default_endpoint(scheme, account_name, default_queue_hostname_prefix, endpoint_suffix) : storage_uri(web::http::uri(queue_endpoint)),
                    table_endpoint.empty() ? construct_default_endpoint(scheme, account_name, default_table_hostname_prefix, endpoint_suffix) : storage_uri(web::http::uri(table_endpoint)));

                account.m_endpoint_suffix = endpoint_suffix;
                return account;
            }
        }

        return cloud_storage_account();
    }

    cloud_storage_account cloud_storage_account::parse_explicit_settings(std::map<utility::string_t, utility::string_t> settings)
    {
        utility::string_t blob_endpoint;
        utility::string_t queue_endpoint;
        utility::string_t table_endpoint;
        get_setting(settings, blob_endpoint_setting_string, blob_endpoint);
        get_setting(settings, queue_endpoint_setting_string, queue_endpoint);
        get_setting(settings, table_endpoint_setting_string, table_endpoint);
        storage_credentials credentials(get_credentials(settings));
        
        if (settings.empty() && (!blob_endpoint.empty() || !queue_endpoint.empty() || !table_endpoint.empty()))
        {
            return cloud_storage_account(credentials,
                blob_endpoint.empty() ? storage_uri() : storage_uri(web::http::uri(blob_endpoint)),
                queue_endpoint.empty() ? storage_uri() : storage_uri(web::http::uri(queue_endpoint)),
                table_endpoint.empty() ? storage_uri() : storage_uri(web::http::uri(table_endpoint)));
        }

        return cloud_storage_account();
    }

    cloud_storage_account cloud_storage_account::parse(const utility::string_t& connection_string)
    {
        cloud_storage_account account;
        auto settings = parse_string_into_settings(connection_string);

        account = parse_devstore_settings(settings);
        if (account.is_initialized())
        {
            get_credentials(settings);
            account.m_settings = settings;
            return account;
        }

        account = parse_defaults_settings(settings);
        if (account.is_initialized())
        {
            get_credentials(settings);
            account.m_settings = settings;
            return account;
        }

        account = parse_explicit_settings(settings);
        if (account.is_initialized())
        {
            get_credentials(settings);
            account.m_settings = settings;
            return account;
        }

        throw std::invalid_argument("connection_string");
    }

    cloud_blob_client cloud_storage_account::create_cloud_blob_client() const
    {
        return cloud_blob_client(m_blob_endpoint, m_credentials);
    }

    cloud_blob_client cloud_storage_account::create_cloud_blob_client(const blob_request_options& default_request_options) const
    {
        return cloud_blob_client(m_blob_endpoint, m_credentials, default_request_options);
    }

    cloud_queue_client cloud_storage_account::create_cloud_queue_client() const
    {
        return cloud_queue_client(m_queue_endpoint, m_credentials);
    }

    cloud_queue_client cloud_storage_account::create_cloud_queue_client(const queue_request_options& default_request_options) const
    {
        return cloud_queue_client(m_queue_endpoint, m_credentials, default_request_options);
    }

    cloud_table_client cloud_storage_account::create_cloud_table_client() const
    {
        return cloud_table_client(m_table_endpoint, m_credentials);
    }

    cloud_table_client cloud_storage_account::create_cloud_table_client(const table_request_options& default_request_options) const
    {
        return cloud_table_client(m_table_endpoint, m_credentials, default_request_options);
    }

    utility::string_t cloud_storage_account::to_string(bool export_secrets)
    {
        if (m_settings.empty())
        {
            if (m_default_endpoints)
            {
                m_settings.insert(std::make_pair(default_endpoints_protocol_setting_string, m_blob_endpoint.primary_uri().scheme()));

                if (!m_endpoint_suffix.empty())
                {
                    m_settings.insert(std::make_pair(endpoint_suffix_setting_string, m_endpoint_suffix));
                }
            }
            else
            {
                if (!m_blob_endpoint.primary_uri().is_empty())
                {
                    m_settings.insert(std::make_pair(blob_endpoint_setting_string, m_blob_endpoint.primary_uri().to_string()));
                }

                if (!m_queue_endpoint.primary_uri().is_empty())
                {
                    m_settings.insert(std::make_pair(queue_endpoint_setting_string, m_queue_endpoint.primary_uri().to_string()));
                }

                if (!m_table_endpoint.primary_uri().is_empty())
                {
                    m_settings.insert(std::make_pair(table_endpoint_setting_string, m_table_endpoint.primary_uri().to_string()));
                }
            }
        }

        bool semicolon = false;
        utility::ostringstream_t result;
        for (auto iter = m_settings.cbegin(); iter != m_settings.cend(); ++iter)
        {
            if (semicolon)
            {
                result << U(';');
            }

            result << iter->first << U('=') << iter->second;
            semicolon = true;
        }

        if (!m_is_development_storage_account)
        {
            if (m_credentials.is_shared_key())
            {
                result << U(';') << account_name_setting_string << U('=') << m_credentials.account_name();
                result << U(';') << account_key_setting_string << U('=') << (export_secrets ? utility::conversions::to_base64(m_credentials.account_key()) : U("[key hidden]"));
            }

            if (m_credentials.is_sas())
            {
                result << U(';') << shared_access_signature_setting_string << U('=') << (export_secrets ? m_credentials.sas_token() : U("[key hidden]"));
            }
        }

        return result.str();
    }

    utility::string_t cloud_storage_account::get_shared_access_signature(const account_shared_access_policy& policy) const
    {
        if (!credentials().is_shared_key())
        {
            throw std::logic_error(protocol::error_sas_missing_credentials);
        }

        return protocol::get_account_sas_token(utility::string_t(), policy, credentials());
    }

}} // namespace azure::storage
