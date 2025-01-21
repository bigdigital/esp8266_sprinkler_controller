String templateProcessor(const String &var)
{
    extern Settings settings;
    if (var == F("p_header"))
        return (FPSTR(HTML_HEAD));
    if (var == F("p_footer"))
        return (FPSTR(HTML_FOOT));  
    if (var == F("p_device_name"))
        return (settings.data.deviceName);
    if (var == F("p_sw_version"))
        return (SW_VERSION); 
    return String(); 
}
